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

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <stddef.h>
#include <string.h>
#include <pthread.h>
#include <limits.h>

#include <utils.h>
#include <tas_sockets.h>
#include <tas_ll.h>

#include "internal.h"
#include "../tas/internal.h"

static inline void ev_listen_open(struct flextcp_context *ctx,
    struct flextcp_event *ev);
static inline void ev_listen_newconn(struct flextcp_context *ctx,
    struct flextcp_event *ev);
static inline void ev_listen_accept(struct flextcp_context *ctx,
    struct flextcp_event *ev);
static inline void ev_conn_open(struct flextcp_context *ctx,
    struct flextcp_event *ev);
static inline void ev_conn_received(struct flextcp_context *ctx,
    struct flextcp_event *ev);
static inline void ev_conn_sendbuf(struct flextcp_context *ctx,
    struct flextcp_event *ev);
static inline void ev_conn_moved(struct flextcp_context *ctx,
    struct flextcp_event *ev);
static inline void ev_listen_moved(struct flextcp_context *ctx,
      struct flextcp_event *ev);
static inline void ev_conn_rxclosed(struct flextcp_context *ctx,
    struct flextcp_event *ev);
static inline void ev_conn_txclosed(struct flextcp_context *ctx,
    struct flextcp_event *ev);
static inline void ev_conn_closed(struct flextcp_context *ctx,
    struct flextcp_event *ev);

static __thread struct sockets_context *local_context;
static pthread_mutex_t context_init_mutex = PTHREAD_MUTEX_INITIALIZER;

void flextcp_local_context_clear(void)
{
  local_context = NULL;
}

struct sockets_context *flextcp_sockctx_getfull(void)
{
  struct sockets_context *ctx = local_context;
  int ret;

  if (ctx == NULL) {
    if ((ctx = calloc(1, sizeof(*ctx))) == NULL) {
      fprintf(stderr, "flextcp socket flextcp_sockctx_get: calloc failed\n");
      abort();
    }

    pthread_mutex_lock(&context_init_mutex);
    ret = flextcp_context_create(&ctx->ctx, NULL, NULL);
    pthread_mutex_unlock(&context_init_mutex);
    if (ret != 0) {
      fprintf(stderr, "flextcp socket flextcp_sockctx_get: flextcp_context_create "
          "failed\n");
      abort();
    }

    local_context = ctx;
  }

  return ctx;

}

struct flextcp_context *flextcp_sockctx_get(void)
{
  return &flextcp_sockctx_getfull()->ctx;
}

int flextcp_sockctx_poll(struct flextcp_context *ctx)
{
  struct flextcp_event evs[16];
  int i, num;

  if ((num = flextcp_context_poll(ctx, sizeof(evs) / sizeof(evs[0]), evs)) < 0) {
    fprintf(stderr, "sockets poll_ctx: flextcp_context_poll failed\n");
    abort();
  }

  for (i = 0; i < num; i++) {
    switch (evs[i].event_type) {
      case FLEXTCP_EV_LISTEN_OPEN:
        ev_listen_open(ctx, &evs[i]);
        break;

      case FLEXTCP_EV_LISTEN_NEWCONN:
        ev_listen_newconn(ctx, &evs[i]);
        break;

      case FLEXTCP_EV_LISTEN_ACCEPT:
        ev_listen_accept(ctx, &evs[i]);
        break;

      case FLEXTCP_EV_CONN_OPEN:
        ev_conn_open(ctx, &evs[i]);
        break;

      case FLEXTCP_EV_CONN_RECEIVED:
        ev_conn_received(ctx, &evs[i]);
        break;

      case FLEXTCP_EV_CONN_SENDBUF:
        ev_conn_sendbuf(ctx, &evs[i]);
        break;

      case FLEXTCP_EV_CONN_MOVED:
        ev_conn_moved(ctx, &evs[i]);
        break;

      case FLEXTCP_EV_LISTEN_MOVED:
        ev_listen_moved(ctx, &evs[i]);
        break;

      case FLEXTCP_EV_CONN_RXCLOSED:
        ev_conn_rxclosed(ctx, &evs[i]);
        break;

      case FLEXTCP_EV_CONN_TXCLOSED:
        ev_conn_txclosed(ctx, &evs[i]);
        break;

      case FLEXTCP_EV_CONN_CLOSED:
        ev_conn_closed(ctx, &evs[i]);
        break;

      default:
        fprintf(stderr, "sockets poll_ctx: unexpected event: %u\n",
            evs[i].event_type);
        break;
    }
  }

  return num;
}

int flextcp_sockctx_poll_n(struct flextcp_context *ctx, unsigned n)
{
  int nevents = 0;

  do {
    nevents += flextcp_sockctx_poll(ctx);
    n = (n >= 16 ? n - 16 : 0);
  } while (n > 0);

  return nevents;
}

static inline void ev_listen_open(struct flextcp_context *ctx,
    struct flextcp_event *ev)
{
  struct flextcp_listener *l;
  struct socket *s;

  l = ev->ev.listen_open.listener;
  s = (struct socket *)
    ((uint8_t *) l - offsetof(struct socket, data.listener.l));

  socket_lock(s);

  assert(s->type == SOCK_LISTENER);
  assert(s->data.listener.status == SOL_OPENING);
  if (ev->ev.listen_open.status == 0) {
    s->data.listener.status = SOL_OPEN;
  } else {
    s->data.listener.status = SOL_FAILED;
  }

  socket_unlock(s);
}

static inline void ev_listen_newconn(struct flextcp_context *ctx,
    struct flextcp_event *ev)
{
}

static inline void ev_listen_accept(struct flextcp_context *ctx,
    struct flextcp_event *ev)
{
  struct flextcp_connection *c;
  struct socket *s, *sl;

  c = ev->ev.listen_accept.conn;
  s = (struct socket *)
    ((uint8_t *) c - offsetof(struct socket, data.connection.c));

  assert(s->type == SOCK_CONNECTION);
  sl = s->data.connection.listener;
  assert(sl != NULL);

  socket_lock(sl);
  socket_lock(s);

  /** Return so we don't duplicate connection acceptance */
  if (s->data.connection.accepted == 1)
  {
    socket_unlock(s);
    socket_unlock(sl);
    return;
  }

  assert(s->data.connection.status == SOC_CONNECTING);
  flextcp_epoll_set(sl, EPOLLIN);

  if (ev->ev.listen_accept.status == 0) {
    s->data.connection.status = SOC_CONNECTED;
    flextcp_epoll_set(s, EPOLLOUT);
  } else {
    s->data.connection.status = SOC_FAILED;
    flextcp_epoll_set(s, EPOLLERR);
  }

  socket_unlock(s);
  socket_unlock(sl);
}


static inline void ev_listen_moved(struct flextcp_context *ctx,
  struct flextcp_event *ev)
{
struct flextcp_listener *l;
struct socket *s;

l = ev->ev.listen_moved.l;
s = (struct socket *)
  ((uint8_t *) l - offsetof(struct socket, data.listener.l));

socket_lock(s);

assert(s->type == SOCK_LISTENER);
assert(s->data.listener.status == SOL_OPEN);

s->data.listener.move_status = ev->ev.listen_moved.status;

socket_unlock(s);
}

static inline void ev_conn_open(struct flextcp_context *ctx,
    struct flextcp_event *ev)
{
  struct flextcp_connection *c;
  struct socket *s;

  c = ev->ev.conn_open.conn;
  s = (struct socket *)
    ((uint8_t *) c - offsetof(struct socket, data.connection.c));

  socket_lock(s);

  assert(s->type == SOCK_CONNECTION);
  assert(s->data.connection.status == SOC_CONNECTING);

  if (ev->ev.conn_open.status == 0) {
    s->data.connection.status = SOC_CONNECTED;
    flextcp_epoll_set(s, EPOLLOUT);
  } else {
    s->data.connection.status = SOC_FAILED;
    flextcp_epoll_set(s, EPOLLERR);
  }

  socket_unlock(s);
}

static inline void ev_conn_received(struct flextcp_context *ctx,
    struct flextcp_event *ev)
{
  struct flextcp_connection *c;
  struct socket *s;
  void *buf;
  size_t len;

  c = ev->ev.conn_received.conn;
  s = (struct socket *)
    ((uint8_t *) c - offsetof(struct socket, data.connection.c));

  socket_lock(s);

  if (s->data.connection.status == SOC_CLOSED) {
    /* ignore data on socket we have already closed */
    goto out;
  }

  assert(s->type == SOCK_CONNECTION);
  assert(s->data.connection.status == SOC_CONNECTED);

  buf = ev->ev.conn_received.buf;
  len = ev->ev.conn_received.len;

  if (s->data.connection.rx_len_1 == 0) {
    /* no data currently ready on socket */
    s->data.connection.rx_len_1 = len;
    s->data.connection.rx_buf_1 = buf;
    s->data.connection.rx_len_2 = 0;
  } else if (s->data.connection.rx_len_2 == 0 && buf ==
      (uint8_t *) s->data.connection.rx_buf_1 + s->data.connection.rx_len_1)
  {
    /* append to previous location 1 */
    s->data.connection.rx_len_1 += len;
  } else if (buf ==
      (uint8_t *) s->data.connection.rx_buf_2 + s->data.connection.rx_len_2)
  {
    /* append to previous location 2 */
    s->data.connection.rx_len_2 += len;
  } else {
    /* because we know that the underlying buffer is circular, we know that
     * there can't be more than 2 positions */
    if (s->data.connection.rx_len_2 != 0) {
      fprintf(stderr, "ev_conn_received: More than two non-contiguous "
          "buffer pieces, this should not happen\n");
      abort();
    }

    s->data.connection.rx_len_2 = len;
    s->data.connection.rx_buf_2 = buf;
  }

  flextcp_epoll_set(s, EPOLLIN);

out:
  socket_unlock(s);
}

static inline void ev_conn_sendbuf(struct flextcp_context *ctx,
    struct flextcp_event *ev)
{
  struct flextcp_connection *c;
  struct socket *s;

  c = ev->ev.conn_sendbuf.conn;
  s = (struct socket *)
    ((uint8_t *) c - offsetof(struct socket, data.connection.c));

  socket_lock(s);

  assert(s->type == SOCK_CONNECTION);
  assert(s->data.connection.status == SOC_CONNECTED);

  flextcp_epoll_set(s, EPOLLOUT);

  socket_unlock(s);
}

static inline void ev_conn_moved(struct flextcp_context *ctx,
    struct flextcp_event *ev)
{
  struct flextcp_connection *c;
  struct socket *s;

  c = ev->ev.conn_moved.conn;
  s = (struct socket *)
    ((uint8_t *) c - offsetof(struct socket, data.connection.c));

  socket_lock(s);

  assert(s->type == SOCK_CONNECTION);
  assert(s->data.connection.status == SOC_CONNECTED);

  s->data.connection.move_status = ev->ev.conn_moved.status;

  socket_unlock(s);
}

static inline void ev_conn_rxclosed(struct flextcp_context *ctx,
    struct flextcp_event *ev)
{
  struct flextcp_connection *c;
  struct socket *s;

  c = ev->ev.conn_rxclosed.conn;
  s = (struct socket *)
    ((uint8_t *) c - offsetof(struct socket, data.connection.c));

  socket_lock(s);

  assert(s->type == SOCK_CONNECTION);
  assert(s->data.connection.status == SOC_CONNECTED ||
      s->data.connection.status == SOC_CLOSED);

  s->data.connection.st_flags |= CSTF_RXCLOSED;
  flextcp_epoll_set(s, EPOLLIN | EPOLLRDHUP);

  if (s->data.connection.status == SOC_CLOSED &&
      (s->data.connection.st_flags & CSTF_TXCLOSED_ACK))
  {
    /* if socket is being closed and both rx and tx are closed now, finish
     * close. */
    flextcp_sockclose_finish(ctx, s);
  }

  socket_unlock(s);
}

static inline void ev_conn_txclosed(struct flextcp_context *ctx,
    struct flextcp_event *ev)
{
  struct flextcp_connection *c;
  struct socket *s;

  c = ev->ev.conn_txclosed.conn;
  s = (struct socket *)
    ((uint8_t *) c - offsetof(struct socket, data.connection.c));

  socket_lock(s);

  assert(s->type == SOCK_CONNECTION);
  assert(s->data.connection.status == SOC_CONNECTED ||
      s->data.connection.status == SOC_CLOSED);

  s->data.connection.st_flags |= CSTF_TXCLOSED_ACK;

  if (s->data.connection.status == SOC_CLOSED &&
      (s->data.connection.st_flags & CSTF_RXCLOSED))
  {
    /* if socket is being closed and both rx and tx are closed now, finish
     * close. */
    flextcp_sockclose_finish(ctx, s);
  }

  socket_unlock(s);
}

static inline void ev_conn_closed(struct flextcp_context *ctx,
    struct flextcp_event *ev)
{
  struct flextcp_connection *c;
  struct socket *s;

  c = ev->ev.conn_closed.conn;
  s = (struct socket *)
    ((uint8_t *) c - offsetof(struct socket, data.connection.c));

  socket_lock(s);

  assert(s->type == SOCK_CONNECTION);
  assert(s->data.connection.status == SOC_CLOSED);

  free(s);
}
