/*
 * Copyright 2019 University of Washington, Max Planck Institute for
 * Software Systems, and The University of Texas at Austin
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <poll.h>
#include <sys/eventfd.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>

#include <tas_ll_connect.h>
#include <kernel_appif.h>
#include <tas_ll.h>
#include <tas_memif.h>
#include <utils_timeout.h>
#include "internal.h"

void *flexnic_mem = NULL;
int flexnic_shmfd = -1;
struct flexnic_info *flexnic_info = NULL;
int flexnic_evfd[FLEXTCP_MAX_FTCPCORES];

static inline int event_kappin_conn_opened(
    struct kernel_appin_conn_opened *inev, struct flextcp_event *outev,
    unsigned avail);
static inline void event_kappin_listen_newconn(
    struct kernel_appin_listen_newconn *inev, struct flextcp_event *outev);
static inline int event_kappin_accept_conn(
    struct kernel_appin_accept_conn *inev, struct flextcp_event *outev,
    unsigned avail);
static inline void event_kappin_st_conn_move(
    struct kernel_appin_status *inev, struct flextcp_event *outev);
static inline void event_kappin_st_listen_open(
    struct kernel_appin_status *inev, struct flextcp_event *outev);
static inline void event_kappin_st_listen_move(
      struct kernel_appin_status *inev, struct flextcp_event *outev);
static inline void event_kappin_st_conn_closed(
    struct kernel_appin_status *inev, struct flextcp_event *outev);

static inline int event_arx_connupdate(struct flextcp_context *ctx,
    volatile struct flextcp_pl_arx_connupdate *inev,
    struct flextcp_event *outevs, int outn, uint16_t fn_core);

static int kernel_poll(struct flextcp_context *ctx, int num,
    struct flextcp_event *events, int *used) __attribute__((noinline));
static int fastpath_poll(struct flextcp_context *ctx, int num,
    struct flextcp_event *events, int *used)
    __attribute__((used,noinline));
static int fastpath_poll_vec(struct flextcp_context *ctx, int num,
    struct flextcp_event *events, int *used) __attribute__((used,noinline));
static void conns_bump(struct flextcp_context *ctx) __attribute__((noinline));
static void txq_probe(struct flextcp_context *ctx, unsigned n) __attribute__((noinline));

int flextcp_init(int groupid)
{
  if ((flextcp_kernel_connect(&flexnic_shmfd, groupid)) < 0)
  {
    fprintf(stderr, "flextcp_init: connecting to kernel failed\n");
    return -1;
  }

  if (flexnic_driver_connect(&flexnic_info, &flexnic_mem, flexnic_shmfd)
      != 0)
  {
    fprintf(stderr, "flextcp_init: connecting to flexnic failed\n");
    return -1;
  }

  return 0;
}


int flextcp_context_create(struct flextcp_context *ctx,
    uint8_t *presp, ssize_t *presp_sz)
{
  static uint16_t ctx_id = 0;

  memset(ctx, 0, sizeof(*ctx));

  ctx->ctx_id = __sync_fetch_and_add(&ctx_id, 1);
  if (ctx->ctx_id >= FLEXTCP_MAX_CONTEXTS) {
    fprintf(stderr, "flextcp_context_create: maximum number of contexts "
        "exeeded\n");
    return -1;
  }

  ctx->evfd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
  if (ctx->evfd < 0) {
    perror("flextcp_context_create: eventfd for waiting fd failed");
    return -1;
  }

  return flextcp_kernel_newctx(ctx, presp, presp_sz);
}

#include <pthread.h>

int debug_flextcp_on = 0;

static int kernel_poll(struct flextcp_context *ctx, int num,
    struct flextcp_event *events, int *used)
{
  int i, j = 0;
  uint32_t pos;
  struct kernel_appin *kout;
  uint8_t type;

  /* poll kernel queues */
  pos = ctx->kout_head;
  for (i = 0; i < num;) {
    kout = (struct kernel_appin *) ctx->kout_base + pos;
    j = 1;

    type = kout->type;
    MEM_BARRIER();

    if (type == KERNEL_APPIN_INVALID) {
      break;
    } else if (type == KERNEL_APPIN_CONN_OPENED) {
      j = event_kappin_conn_opened(&kout->data.conn_opened, &events[i],
          num - i);
    } else if (type == KERNEL_APPIN_LISTEN_NEWCONN) {
      event_kappin_listen_newconn(&kout->data.listen_newconn, &events[i]);
    } else if (type == KERNEL_APPIN_ACCEPTED_CONN) {
      j = event_kappin_accept_conn(&kout->data.accept_connection, &events[i],
          num - i);
    } else if (type == KERNEL_APPIN_STATUS_LISTEN_OPEN) {
      event_kappin_st_listen_open(&kout->data.status, &events[i]);
    } else if (type == KERNEL_APPIN_STATUS_CONN_MOVE) {
      event_kappin_st_conn_move(&kout->data.status, &events[i]);
    } else if (KERNEL_APPIN_STATUS_LISTEN_MOVE) {
      event_kappin_st_listen_move(&kout->data.status, &events[i]);
    } else if (type == KERNEL_APPIN_STATUS_CONN_CLOSE) {
      event_kappin_st_conn_closed(&kout->data.status, &events[i]);
    } else {
      fprintf(stderr, "flextcp_context_poll: unexpected kout type=%u pos=%u len=%u\n",
          type, pos, ctx->kout_len);
      abort();
    }
    ctx->flags |= CTX_FLAG_POLL_EVENTS;

    if (j == -1) {
      break;
    }

    i += j;

    MEM_BARRIER();
    kout->type = KERNEL_APPIN_INVALID;

    pos = pos + 1;
    if (pos >= ctx->kout_len) {
      pos = 0;
    }
  }
  ctx->kout_head = pos;

  *used = i;
  return (j == -1 ? -1 : 0);
}

static int fastpath_poll(struct flextcp_context *ctx, int num,
    struct flextcp_event *events, int *used)
{
  int i, j, ran_out;
  volatile struct flextcp_pl_arx *arx_q, *arx;
  uint32_t head;
  uint16_t k;

  i = 0;
  for (k = 0; k < ctx->num_queues && i < num; k++) {
    ran_out = 0;

    arx_q = (volatile struct flextcp_pl_arx *)
      ctx->queues[ctx->next_queue].rxq_base;
    head = ctx->queues[ctx->next_queue].rxq_head;
    for (; i < num;) {
      j = 0;
      arx = &arx_q[head / sizeof(*arx)];
      if (arx->type == FLEXTCP_PL_ARX_INVALID) {
        break;
      } else if (arx->type == FLEXTCP_PL_ARX_CONNUPDATE) {
        j = event_arx_connupdate(ctx, &arx->msg.connupdate, events + i, num - i, ctx->next_queue);
      } else {
        fprintf(stderr, "flextcp_context_poll: kout type=%u head=%x\n", arx->type, head);
      }
      ctx->flags |= CTX_FLAG_POLL_EVENTS;

      if (j == -1) {
        ran_out = 1;
        break;
      }
      i += j;

      arx->type = 0;

      /* next entry */
      head += sizeof(*arx);
      if (head >= ctx->rxq_len) {
          head -= ctx->rxq_len;
      }
    }

    ctx->queues[ctx->next_queue].rxq_head = head;
    if (ran_out) {
      *used = i;
      return -1;
    }

    ctx->next_queue = ctx->next_queue + 1;
    if (ctx->next_queue >= ctx->num_queues)
      ctx->next_queue -= ctx->num_queues;
  }

  *used = i;
  return 0;
}

static inline void fetch_8ts(struct flextcp_context *ctx, uint32_t *heads,
    uint16_t q, uint8_t *ts)
{
  struct flextcp_pl_arx *p0, *p1, *p2, *p3, *p4, *p5, *p6, *p7;

  p0 = (struct flextcp_pl_arx *) (ctx->queues[q].rxq_base + heads[q]);
  q = (q + 1 < ctx->num_queues ? q + 1 : 0);
  p1 = (struct flextcp_pl_arx *) (ctx->queues[q].rxq_base + heads[q]);
  q = (q + 1 < ctx->num_queues ? q + 1 : 0);
  p2 = (struct flextcp_pl_arx *) (ctx->queues[q].rxq_base + heads[q]);
  q = (q + 1 < ctx->num_queues ? q + 1 : 0);
  p3 = (struct flextcp_pl_arx *) (ctx->queues[q].rxq_base + heads[q]);
  q = (q + 1 < ctx->num_queues ? q + 1 : 0);
  p4 = (struct flextcp_pl_arx *) (ctx->queues[q].rxq_base + heads[q]);
  q = (q + 1 < ctx->num_queues ? q + 1 : 0);
  p5 = (struct flextcp_pl_arx *) (ctx->queues[q].rxq_base + heads[q]);
  q = (q + 1 < ctx->num_queues ? q + 1 : 0);
  p6 = (struct flextcp_pl_arx *) (ctx->queues[q].rxq_base + heads[q]);
  q = (q + 1 < ctx->num_queues ? q + 1 : 0);
  p7 = (struct flextcp_pl_arx *) (ctx->queues[q].rxq_base + heads[q]);
  q = (q + 1 < ctx->num_queues ? q + 1 : 0);

  asm volatile(
      "prefetcht0 32(%0);"
      "prefetcht0 32(%1);"
      "prefetcht0 32(%2);"
      "prefetcht0 32(%3);"
      "prefetcht0 32(%4);"
      "prefetcht0 32(%5);"
      "prefetcht0 32(%6);"
      "prefetcht0 32(%7);"
      "movb 31(%0), %b0;"
      "movb 31(%1), %b1;"
      "movb 31(%2), %b2;"
      "movb 31(%3), %b3;"
      "movb 31(%4), %b4;"
      "movb 31(%5), %b5;"
      "movb 31(%6), %b6;"
      "movb 31(%7), %b7;"

      "movb %b0, 0(%8);"
      "movb %b1, 1(%8);"
      "movb %b2, 2(%8);"
      "movb %b3, 3(%8);"
      "movb %b4, 4(%8);"
      "movb %b5, 5(%8);"
      "movb %b6, 6(%8);"
      "movb %b7, 7(%8);"
      :
      : "r" (p0), "r" (p1), "r" (p2), "r" (p3),
        "r" (p4), "r" (p5), "r" (p6), "r" (p7), "r" (ts)
      : "memory");

}

static inline void fetch_4ts(struct flextcp_context *ctx, uint32_t *heads,
    uint16_t q, uint8_t *ts)
{
  struct flextcp_pl_arx *p0, *p1, *p2, *p3;

  p0 = (struct flextcp_pl_arx *) (ctx->queues[q].rxq_base + heads[q]);
  q = (q + 1 < ctx->num_queues ? q + 1 : 0);
  p1 = (struct flextcp_pl_arx *) (ctx->queues[q].rxq_base + heads[q]);
  q = (q + 1 < ctx->num_queues ? q + 1 : 0);
  p2 = (struct flextcp_pl_arx *) (ctx->queues[q].rxq_base + heads[q]);
  q = (q + 1 < ctx->num_queues ? q + 1 : 0);
  p3 = (struct flextcp_pl_arx *) (ctx->queues[q].rxq_base + heads[q]);
  q = (q + 1 < ctx->num_queues ? q + 1 : 0);

  asm volatile(
      "prefetcht0 32(%0);"
      "prefetcht0 32(%1);"
      "prefetcht0 32(%2);"
      "prefetcht0 32(%3);"
      "movb 31(%0), %b0;"
      "movb 31(%1), %b1;"
      "movb 31(%2), %b2;"
      "movb 31(%3), %b3;"
      "movb %b0, 0(%4);"
      "movb %b1, 1(%4);"
      "movb %b2, 2(%4);"
      "movb %b3, 3(%4);"
      :
      : "r" (p0), "r" (p1), "r" (p2), "r" (p3), "r" (ts)
      : "memory");
}


static int fastpath_poll_vec(struct flextcp_context *ctx, int num,
    struct flextcp_event *events, int *used)
{
  int i, j, ran_out, found, found_inner;
  volatile struct flextcp_pl_arx *arx;
  uint32_t head;
  uint16_t l, k, q;
  uint8_t t;
  uint8_t types[ctx->num_queues];
  uint32_t qheads[ctx->num_queues];

  volatile struct flextcp_pl_arx *arxs[num];
  uint8_t arx_qs[num];

  for (q = 0; q < ctx->num_queues; q++) {
    qheads[q] = ctx->queues[q].rxq_head;
  }

  ran_out = found = 0;
  i = 0;
  q = ctx->next_queue;
  while (i < num && !ran_out) {
    l = 0;
    for (found_inner = 1; found_inner && i + l < num; ) {
      found_inner = 0;

      /* fetch types from all n queues */
      uint16_t qs = ctx->num_queues;
      q = ctx->next_queue;
      k = 0;
      while (qs > 8) {
        fetch_8ts(ctx, qheads, q, types + k);

        q = (q + 8 < ctx->num_queues ? q + 8 : q + 8 - ctx->num_queues);
        k += 8;
        qs -= 8;
      }
      while (qs > 4) {
        fetch_4ts(ctx, qheads, q, types + k);

        q = (q + 4 < ctx->num_queues ? q + 4 : q + 4 - ctx->num_queues);
        k += 4;
        qs -= 4;
      }
      while (qs > 0) {
        arx = (volatile struct flextcp_pl_arx *)
          (ctx->queues[q].rxq_base + qheads[q]);
        q = (q + 1 < ctx->num_queues ? q + 1 : 0);
        types[k] = arx->type;
        k++;
        qs--;
      }

      /* prefetch connection state for all entries */
      for (k = 0, q = ctx->next_queue; k < ctx->num_queues && i + l < num; k++) {
        if (types[k] == FLEXTCP_PL_ARX_CONNUPDATE) {
          arx = (volatile struct flextcp_pl_arx *)
            (ctx->queues[q].rxq_base + qheads[q]);
          util_prefetch0(OPAQUE_PTR(arx->msg.connupdate.opaque) + 64);
          util_prefetch0(OPAQUE_PTR(arx->msg.connupdate.opaque));

          arxs[l] = arx;
          arx_qs[l] = q;
          l++;
          found_inner = 1;

          qheads[q] = qheads[q] + sizeof(*arx);
          if (qheads[q] >= ctx->rxq_len) {
            qheads[q] -= ctx->rxq_len;
          }
        }
        q = (q + 1 < ctx->num_queues ? q + 1 : 0);
      }
    }

    if (l == 0)
      break;

    for (k = 0; k < l && i < num; k++) {
      arx = arxs[k];
      q = arx_qs[k];
      head = ctx->queues[q].rxq_head;

      t = arx->type;
      assert(t != FLEXTCP_PL_ARX_INVALID);

      if (t == FLEXTCP_PL_ARX_CONNUPDATE) {
        j = event_arx_connupdate(ctx, &arx->msg.connupdate, events + i,
            num - i, q);
      } else {
        j = 0;
        fprintf(stderr, "flextcp_context_poll: kout type=%u head=%x\n",
            arx->type, head);
      }

      found = 1;

      if (j == -1) {
        ran_out = 1;
        break;
      }
      i += j;

      arx->type = 0;

      /* next entry */
      head += sizeof(*arx);
      if (head >= ctx->rxq_len) {
        head -= ctx->rxq_len;
      }
      ctx->queues[q].rxq_head = head;
    }
    q = (q + 1 < ctx->num_queues ? q + 1 : 0);
  }

  ctx->next_queue = q;

  if (found) {
    for (k = 0, q = ctx->next_queue; k < ctx->num_queues; k++) {
      arx = (struct flextcp_pl_arx *) (ctx->queues[q].rxq_base +
          ctx->queues[q].rxq_head);
      util_prefetch0(arx);
      q = (q + 1 < ctx->num_queues ? q + 1 : 0);
    }

    ctx->flags |= CTX_FLAG_POLL_EVENTS;
  }

  *used = i;
  return 0;
}


int flextcp_context_poll(struct flextcp_context *ctx, int num,
    struct flextcp_event *events)
{
  int i, j;

  i = 0;

  ctx->flags |= CTX_FLAG_POLL_CALLED;

  /* prefetch queues */
  uint32_t k, q;
  for (k = 0, q = ctx->next_queue; k < ctx->num_queues; k++) {
    util_prefetch0((struct flextcp_pl_arx *) (ctx->queues[q].rxq_base +
        ctx->queues[q].rxq_head));
    q = (q + 1 < ctx->num_queues ? q + 1 : 0);
  }

  /* poll kernel */
  if (kernel_poll(ctx, num, events, &i) == -1) {
    /* not enough event space, abort */
    return i;
  }

  /* poll NIC queues */
  fastpath_poll_vec(ctx, num - i, events + i, &j);

  txq_probe(ctx, num);
  conns_bump(ctx);

  return i + j;
}

int flextcp_context_tx_alloc(struct flextcp_context *ctx,
    struct flextcp_pl_atx **patx, uint16_t core)
{
  /* if queue is full, abort */
  if (ctx->queues[core].txq_avail == 0) {
    return -1;
  }

  *patx = (struct flextcp_pl_atx *)
    (ctx->queues[core].txq_base + ctx->queues[core].txq_tail);
  return 0;
}

static void flextcp_flexnic_kick(struct flextcp_context *ctx, int core)
{
  uint64_t now = util_rdtsc();

  if (flexnic_info->poll_cycle_tas == UINT64_MAX) {
    /* blocking for TAS disabled */
    return;
  }

  if(now - ctx->queues[core].last_ts > flexnic_info->poll_cycle_tas) {
    // Kick
    uint64_t val = 1;
    int r = write(flexnic_evfd[core], &val, sizeof(uint64_t));
    assert(r == sizeof(uint64_t));
  }

  ctx->queues[core].last_ts = now;
}

void flextcp_context_tx_done(struct flextcp_context *ctx, uint16_t core)
{
  ctx->queues[core].txq_tail += sizeof(struct flextcp_pl_atx);
  if (ctx->queues[core].txq_tail >= ctx->txq_len) {
    ctx->queues[core].txq_tail -= ctx->txq_len;
  }

  ctx->queues[core].txq_avail -= sizeof(struct flextcp_pl_atx);

  flextcp_flexnic_kick(ctx, core);
}

static inline int event_kappin_conn_opened(
    struct kernel_appin_conn_opened *inev, struct flextcp_event *outev,
    unsigned avail)
{
  struct flextcp_connection *conn;
  int j = 1;

  conn = OPAQUE_PTR(inev->opaque);

  outev->event_type = FLEXTCP_EV_CONN_OPEN;
  outev->ev.conn_open.status = inev->status;
  outev->ev.conn_open.conn = conn;

  if (inev->status != 0) {
    conn->status = CONN_CLOSED;
    return 1;
  } else if (conn->rxb_used > 0 && conn->rx_closed && avail < 3) {
    /* if we've already received updates, we'll need to inject them */
    return -1;
  } else if ((conn->rxb_used > 0 || conn->rx_closed) && avail < 2) {
    /* if we've already received updates, we'll need to inject them */
    return -1;
  }

  conn->status = CONN_OPEN;
  conn->out_remote_ip = inev->out_remote_ip;
  conn->out_local_ip = inev->out_local_ip;
  conn->in_local_ip = inev->in_local_ip;
  conn->local_port = inev->local_port;
  conn->seq_rx = inev->seq_rx;
  conn->seq_tx = inev->seq_tx;
  conn->flow_id = inev->flow_id;
  conn->tunnel_id = inev->tunnel_id;
  conn->fn_core = inev->fn_core;

  conn->rxb_base = (uint8_t *) flexnic_mem + inev->rx_off;
  conn->rxb_len = inev->rx_len;

  conn->txb_base = (uint8_t *) flexnic_mem + inev->tx_off;
  conn->txb_len = inev->tx_len;

  /* inject bump if necessary */
  if (conn->rxb_used > 0) {
    conn->seq_rx += conn->rxb_used;

    outev[j].event_type = FLEXTCP_EV_CONN_RECEIVED;
    outev[j].ev.conn_received.conn = conn;
    outev[j].ev.conn_received.buf = conn->rxb_base;
    outev[j].ev.conn_received.len = conn->rxb_used;
    j++;
  }

  /* add end of stream notification if necessary */
  if (conn->rx_closed) {
    outev[j].event_type = FLEXTCP_EV_CONN_RXCLOSED;
    outev[j].ev.conn_rxclosed.conn = conn;
    j++;
  }

  return j;
}

static inline void event_kappin_listen_newconn(
    struct kernel_appin_listen_newconn *inev, struct flextcp_event *outev)
{
  struct flextcp_listener *listener;

  listener = OPAQUE_PTR(inev->opaque);


  outev->event_type = FLEXTCP_EV_LISTEN_NEWCONN;
  outev->ev.listen_newconn.tunnel_id = inev->tunnel_id;
  outev->ev.listen_newconn.out_remote_ip = inev->out_remote_ip;
  outev->ev.listen_newconn.in_remote_ip = inev->in_remote_ip;
  outev->ev.listen_newconn.remote_port = inev->remote_port;
  outev->ev.listen_open.listener = listener;
}

static inline int event_kappin_accept_conn(
    struct kernel_appin_accept_conn *inev, struct flextcp_event *outev,
    unsigned avail)
{
  struct flextcp_connection *conn;
  int j = 1;

  conn = OPAQUE_PTR(inev->opaque);

  outev->event_type = FLEXTCP_EV_LISTEN_ACCEPT;
  outev->ev.listen_accept.status = inev->status;
  outev->ev.listen_accept.conn = conn;

  if (inev->status != 0) {
    conn->status = CONN_CLOSED;
    return 1;
  } else if (conn->rxb_used > 0 && conn->rx_closed && avail < 3) {
    /* if we've already received updates, we'll need to inject them */
    return -1;
  } else if ((conn->rxb_used > 0 || conn->rx_closed) && avail < 2) {
    /* if we've already received updates, we'll need to inject them */
    return -1;
  }

  conn->status = CONN_OPEN;
  conn->out_local_ip = inev->out_local_ip;
  conn->out_remote_ip = inev->out_remote_ip;
  conn->in_local_ip = inev->in_local_ip;
  conn->in_remote_ip = inev->in_remote_ip;
  conn->remote_port = inev->remote_port;
  conn->seq_rx = inev->seq_rx;
  conn->seq_tx = inev->seq_tx;
  conn->flow_id = inev->flow_id;
  conn->tunnel_id = inev->tunnel_id;
  conn->fn_core = inev->fn_core;

  conn->rxb_base = (uint8_t *) flexnic_mem + inev->rx_off;
  conn->rxb_len = inev->rx_len;

  conn->txb_base = (uint8_t *) flexnic_mem + inev->tx_off;
  conn->txb_len = inev->tx_len;

  /* inject bump if necessary */
  if (conn->rxb_used > 0) {
    conn->seq_rx += conn->rxb_used;

    outev[j].event_type = FLEXTCP_EV_CONN_RECEIVED;
    outev[j].ev.conn_received.conn = conn;
    outev[j].ev.conn_received.buf = conn->rxb_base;
    outev[j].ev.conn_received.len = conn->rxb_used;
    j++;
  }

  /* add end of stream notification if necessary */
  if (conn->rx_closed) {
    outev[j].event_type = FLEXTCP_EV_CONN_RXCLOSED;
    outev[j].ev.conn_rxclosed.conn = conn;
    j++;
  }

  return j;
}

static inline void event_kappin_st_conn_move(
    struct kernel_appin_status *inev, struct flextcp_event *outev)
{
  struct flextcp_connection *conn;

  conn = OPAQUE_PTR(inev->opaque);

  outev->event_type = FLEXTCP_EV_CONN_MOVED;
  outev->ev.conn_moved.status = inev->status;
  outev->ev.conn_moved.conn = conn;
}

static inline void event_kappin_st_listen_move(
  struct kernel_appin_status *inev, struct flextcp_event *outev)
{
  struct flextcp_listener *l;

  l = OPAQUE_PTR(inev->opaque);

  outev->event_type = FLEXTCP_EV_LISTEN_MOVED;
  outev->ev.listen_moved.status = inev->status;
  outev->ev.listen_moved.l = l;
}

static inline void event_kappin_st_listen_open(
    struct kernel_appin_status *inev, struct flextcp_event *outev)
{
  struct flextcp_listener *listener;

  listener = OPAQUE_PTR(inev->opaque);

  outev->event_type = FLEXTCP_EV_LISTEN_OPEN;
  outev->ev.listen_open.status = inev->status;
  outev->ev.listen_open.listener = listener;
}

static inline void event_kappin_st_conn_closed(
    struct kernel_appin_status *inev, struct flextcp_event *outev)
{
  struct flextcp_connection *conn;

  conn = OPAQUE_PTR(inev->opaque);

  outev->event_type = FLEXTCP_EV_CONN_CLOSED;
  outev->ev.conn_closed.status = inev->status;
  outev->ev.conn_closed.conn = conn;

  conn->status = CONN_CLOSED;
}

static inline int event_arx_connupdate(struct flextcp_context *ctx,
    volatile struct flextcp_pl_arx_connupdate *inev,
    struct flextcp_event *outevs, int outn, uint16_t fn_core)
{
  struct flextcp_connection *conn;
  uint32_t rx_bump, rx_len, tx_bump, tx_sent;
  int i = 0, evs_needed, tx_avail_ev, eos;

  conn = OPAQUE_PTR(inev->opaque);

  conn->fn_core = fn_core;

  rx_bump = inev->rx_bump;
  tx_bump = inev->tx_bump;
  eos = ((inev->flags & FLEXTCP_PL_ARX_FLRXDONE) == FLEXTCP_PL_ARX_FLRXDONE);

  if (conn->status == CONN_OPEN_REQUESTED ||
      conn->status == CONN_ACCEPT_REQUESTED)
  {
    /* due to a race we might see connection updates before we see the
     * connection confirmation from the kernel */
    assert(tx_bump == 0);
    conn->rx_closed = !!eos;
    conn->rxb_head += rx_bump;
    conn->rxb_used += rx_bump;
    /* TODO: should probably handle eos here as well */
    return 0;
  } else if (conn->status == CONN_CLOSED ||
      conn->status == CONN_CLOSE_REQUESTED)
  {
    /* just drop bumps for closed connections */
    return 0;
  }

  assert(conn->status == CONN_OPEN);

  /* figure out how many events for rx */
  evs_needed = 0;
  if (rx_bump > 0) {
    evs_needed++;
    if (conn->rxb_head + rx_bump > conn->rxb_len) {
      evs_needed++;
    }
  }

  /* if tx buffer was depleted, we'll generate a tx avail event */
  tx_avail_ev = (tx_bump > 0 && flextcp_conn_txbuf_available(conn) == 0);
  if (tx_avail_ev) {
    evs_needed++;
  }

  tx_sent = conn->txb_sent - tx_bump;

  /* if tx close was acked, also add that event */
  if ((conn->flags & CONN_FLAG_TXEOS_ALLOC) == CONN_FLAG_TXEOS_ALLOC &&
      !tx_sent)
  {
    evs_needed++;
  }

  /* if receive stream closed need additional event */
  if (eos) {
    evs_needed++;
  }

  /* if we can't fit all events, try again later */
  if (evs_needed > outn) {
    return -1;
  }

  /* generate rx events */
  if (rx_bump > 0) {
    outevs[i].event_type = FLEXTCP_EV_CONN_RECEIVED;
    outevs[i].ev.conn_received.conn = conn;
    outevs[i].ev.conn_received.buf = conn->rxb_base + conn->rxb_head;
    util_prefetch0(conn->rxb_base + conn->rxb_head);
    if (conn->rxb_head + rx_bump > conn->rxb_len) {
      /* wrap around in rx buffer */
      rx_len = conn->rxb_len - conn->rxb_head;
      outevs[i].ev.conn_received.len = rx_len;

      i++;
      outevs[i].event_type = FLEXTCP_EV_CONN_RECEIVED;
      outevs[i].ev.conn_received.conn = conn;
      outevs[i].ev.conn_received.buf = conn->rxb_base;
      outevs[i].ev.conn_received.len = rx_bump - rx_len;
    } else {
      outevs[i].ev.conn_received.len = rx_bump;
    }
    i++;

    /* update rx buffer */
    conn->seq_rx += rx_bump;
    conn->rxb_head += rx_bump;
    if (conn->rxb_head >= conn->rxb_len) {
      conn->rxb_head -= conn->rxb_len;
    }
    conn->rxb_used += rx_bump;
  }

  /* bump tx */
  if (tx_bump > 0) {
    conn->txb_sent -= tx_bump;

    if (tx_avail_ev) {
      outevs[i].event_type = FLEXTCP_EV_CONN_SENDBUF;
      outevs[i].ev.conn_sendbuf.conn = conn;
      i++;
    }

    /* if we were previously unable to push out TX EOS, do so now. */
    if ((conn->flags & CONN_FLAG_TXEOS) == CONN_FLAG_TXEOS &&
        !(conn->flags & CONN_FLAG_TXEOS_ALLOC))
    {
      if (flextcp_conn_pushtxeos(ctx, conn) != 0) {
        /* should never happen */
        fprintf(stderr, "event_arx_connupdate: flextcp_conn_pushtxeos "
            "failed\n");
        abort();
      }
    } else if ((conn->flags & CONN_FLAG_TXEOS_ALLOC) == CONN_FLAG_TXEOS_ALLOC) {
      /* There should be no data after we push out the EOS */
      assert(!(conn->flags & CONN_FLAG_TXEOS_ACK));

      /* if this was the last bump, mark TX EOS as acked */
      if (conn->txb_sent == 0) {
        conn->flags |= CONN_FLAG_TXEOS_ACK;

        outevs[i].event_type = FLEXTCP_EV_CONN_TXCLOSED;
        outevs[i].ev.conn_txclosed.conn = conn;
        i++;
      }
    }
  }

  /* add end of stream notification */
  if (eos) {
    outevs[i].event_type = FLEXTCP_EV_CONN_RXCLOSED;
    outevs[i].ev.conn_rxclosed.conn = conn;
    conn->rx_closed = 1;
    i++;
  }

  return i;
}


static void txq_probe(struct flextcp_context *ctx, unsigned n)
{
  struct flextcp_pl_atx *atx;
  uint32_t pos, i, q, tail, avail, len;

  len = ctx->txq_len;
  for (q = 0; q < ctx->num_queues; q++) {
    avail = ctx->queues[q].txq_avail;

    if (avail > len / 2)
      continue;

    tail = ctx->queues[q].txq_tail;

    pos = tail + avail;
    if (pos >= len)
      pos -= len;

    i = 0;
    while (avail < len && i < 2 * n) {
      atx = (struct flextcp_pl_atx *) (ctx->queues[q].txq_base + pos);

      if (atx->type != 0) {
        break;
      }

      avail += sizeof(*atx);
      pos += sizeof(*atx);
      if (pos >= len)
        pos -= len;
      i++;

      MEM_BARRIER();
    }

    ctx->queues[q].txq_avail = avail;
  }
}

static void conns_bump(struct flextcp_context *ctx)
{
  struct flextcp_connection *c;
  struct flextcp_pl_atx *atx;
  uint8_t flags;

  while ((c = ctx->bump_pending_first) != NULL) {
    assert(c->status == CONN_OPEN);

    if (flextcp_context_tx_alloc(ctx, &atx, c->fn_core) != 0) {
      break;
    }

    flags = 0;

    if ((c->flags & CONN_FLAG_TXEOS_ALLOC) == CONN_FLAG_TXEOS_ALLOC) {
      flags |= FLEXTCP_PL_ATX_FLTXDONE;
    }

    atx->msg.connupdate.rx_bump = c->rxb_bump;
    atx->msg.connupdate.tx_bump = c->txb_bump;
    atx->msg.connupdate.flow_id = c->flow_id;
    atx->msg.connupdate.bump_seq = c->bump_seq++;
    atx->msg.connupdate.flags = flags;
    MEM_BARRIER();
    atx->type = FLEXTCP_PL_ATX_CONNUPDATE;

    flextcp_context_tx_done(ctx, c->fn_core);

    c->rxb_bump = c->txb_bump = 0;
    c->bump_pending = 0;

    if (c->bump_next == NULL) {
      ctx->bump_pending_last = NULL;
    }
    ctx->bump_pending_first = c->bump_next;
  }
}

int flextcp_context_waitfd(struct flextcp_context *ctx)
{
  return ctx->evfd;
}

int flextcp_context_canwait(struct flextcp_context *ctx)
{
  /* At a high level this code implements a state machine that ensures that at
   * least POLL_CYCLE time has elapsed between two unsuccessfull poll calls.
   * This is a bit messier because we don't want to move any of the timestamp
   * code into the poll call and make it more expensive for apps that don't
   * block. Instead we use the timing of calls to canwait along with flags that
   * the poll call sets when it's called and when it finds events.
   */

  /* if blocking is disabled, we can never wait */
  if (flexnic_info->poll_cycle_app == UINT64_MAX) {
    return -1;
  }

  /* if there were events found in the last poll, it's back to square one. */
  if ((ctx->flags & CTX_FLAG_POLL_EVENTS) != 0) {
    ctx->flags &= ~(CTX_FLAG_POLL_EVENTS | CTX_FLAG_WANTWAIT |
        CTX_FLAG_LASTWAIT);

    return -1;
  }

  /* from here on we know that there are no events */

  if ((ctx->flags & CTX_FLAG_WANTWAIT) != 0) {
    /* in want wait state: just wait for grace period to be over */
    if ((util_rdtsc() - ctx->last_inev_ts) > flexnic_info->poll_cycle_app) {
      /* past grace period, move on to lastwait. clear polled flag, to make sure
       * it gets polled again before we clear lastwait. */
      ctx->flags &= ~(CTX_FLAG_POLL_CALLED | CTX_FLAG_WANTWAIT);
      ctx->flags |= CTX_FLAG_LASTWAIT;
    }
  } else if ((ctx->flags & CTX_FLAG_LASTWAIT) != 0) {
    /* in last wait state */
    if ((ctx->flags & CTX_FLAG_POLL_CALLED) != 0) {
      /* if we have polled once more after the grace period, we're good to go to
       * sleep */
      return 0;
    }
  } else if ((ctx->flags & CTX_FLAG_POLL_CALLED) != 0) {
    /* not currently getting ready to wait, so start */
    ctx->last_inev_ts = util_rdtsc();
    ctx->flags |= CTX_FLAG_WANTWAIT;
  }

  return -1;
}

void flextcp_context_waitclear(struct flextcp_context *ctx)
{
  ssize_t ret;
  uint64_t val;

  ret = read(ctx->evfd, &val, sizeof(uint64_t));
  if ((ret >= 0 && ret != sizeof(uint64_t)) ||
      (ret < 0 && errno != EAGAIN && errno != EWOULDBLOCK))
  {
    perror("flextcp_context_waitclear: read failed");
    abort();
  }

  ctx->flags &= ~(CTX_FLAG_WANTWAIT | CTX_FLAG_LASTWAIT | CTX_FLAG_POLL_CALLED);
}

int flextcp_context_wait(struct flextcp_context *ctx, int timeout_ms)
{
  struct pollfd pfd;
  int ret;

  if (flextcp_context_canwait(ctx) != 0) {
    return -1;
  }

  pfd.fd = ctx->evfd;
  pfd.events = POLLIN;
  pfd.revents = 0;
  ret = poll(&pfd, 1, timeout_ms);
  if (ret < 0) {
    perror("flextcp_context_wait: poll returned error");
    return -1;
  }

  flextcp_context_waitclear(ctx);
  return 0;
}
