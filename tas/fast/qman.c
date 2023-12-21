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

/**
 * Complete queue manager implementation
 */
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <unistd.h>

#include <rte_config.h>
#include <rte_malloc.h>
#include <rte_cycles.h>

#include <utils.h>
#include <virtuoso.h>
#include <utils_sync.h>

#include "internal.h"
#include "../slow/internal.h"


#define dprintf(...) do { } while (0)

#define FLAG_INSKIPLIST 1
#define FLAG_INNOLIMITL 2

/** Skiplist: bits per level */
#define SKIPLIST_BITS 3

#define RNG_SEED 0x12345678
#define TIMESTAMP_BITS 32
#define TIMESTAMP_MASK 0xFFFFFFFF

/** Queue container for a virtual machine */
struct vm_qman {
  /** VM queue */
  struct vm_queue *queues;
  /** Idx of head of queue */
  uint32_t head_idx;
  /** Idx of tail of queue */
  uint32_t tail_idx;
};

/** Queue container for a flow **/
struct flow_qman {
  /** Flow queue */ 
  struct flow_queue *queues;
  /** Idx of heads of each level in the skiplist */
  uint32_t head_idx[QMAN_SKIPLIST_LEVELS];
  /** Idx of head of no limit queue */
  uint32_t nolimit_head_idx;
  /** Idx of tail of no limit queue */
  uint32_t nolimit_tail_idx;
  /** Whether to poll nolimit queue first */
  bool nolimit_first;
};

/** Queue state for a virtual machine */
struct vm_queue {
  /** Id of this VM */
  uint32_t id;
  /** Next pointer */
  uint32_t next_idx;
  /** Pointer to container with flows for this VM */
  struct flow_qman *fqman;
  /** Number of entries for this VM */
  uint32_t avail;
  /** Flags: FLAG_INNOLIMITL */
  uint16_t flags;
  /* Bytes sent in this round for this VM. Reset every round. */
  uint16_t bytes;
  /** Real timestamp */
  uint32_t ts_real;
  /** Virtual timestamp */
  uint32_t ts_virtual;
};

struct skiplist_fstate {
  /* The current timestamp for this send round */
  uint32_t cur_ts;
  /* Signals if any of the flows for a VM was rate limited */
  uint8_t rate_limited;
  /** The original number of packets requested. Probably batch size */
  unsigned int orig_num;
};

/** Queue state for flow */
struct flow_queue {
  /** Next pointers for levels in skip list */
  uint32_t next_idxs[QMAN_SKIPLIST_LEVELS];
  /** Time stamp */
  uint32_t next_ts;
  /** Assigned Rate */
  uint32_t rate;
  /** Number of entries in queue */
  uint32_t avail;
  /** Maximum chunk size when de-queueing */
  uint16_t max_chunk;
  /** Flags: FLAG_INSKIPLIST, FLAG_INNOLIMITL */
  uint16_t flags;
} __attribute__((packed));
STATIC_ASSERT((sizeof(struct flow_queue) == 32), queue_size);

/** General qman functions */
static inline int64_t rel_time(uint32_t cur_ts, uint32_t ts_in);
static inline uint32_t timestamp(void);
static inline int timestamp_lessthaneq(struct vm_queue *vq, uint32_t a,
    uint32_t b);

/** Qman functions for VM */
static inline int vmcont_init(struct qman_thread *t);
static inline int vm_qman_poll(struct dataplane_context *ctx,
    struct skiplist_fstate *skpl_state, unsigned num, 
    unsigned *vm_id, unsigned *q_ids, uint16_t *q_bytes);
static inline int vm_qman_set(struct qman_thread *t, uint32_t vm_id, uint32_t flow_id,
    uint32_t rate, uint32_t avail, uint16_t max_chunk, uint8_t flags);
static inline void vm_queue_fire(struct vm_qman *vqman, struct vm_queue *q,
    uint32_t idx, uint16_t *q_bytes, int bytes_sum,
    unsigned start, unsigned end);
/** Actually update queue state for app queue */
static inline void vm_set_impl(struct vm_qman *vqman, uint32_t v_idx,
    uint32_t f_idx, uint32_t avail, uint8_t flags);
static inline void vm_queue_activate(struct vm_qman *vqman,
    struct vm_queue *q, uint32_t idx);

/** Qman management functions for flows */
static inline int flowcont_init(struct vm_queue *vq);
static inline int flow_qman_poll(struct qman_thread *t, 
    struct vm_queue *vqueue, struct flow_qman *fqman, 
    struct skiplist_fstate *skpl_state, unsigned num, unsigned *q_ids,
    uint16_t *q_bytes, uint32_t *vm_ids, int *bytes_sum);
int flow_qman_set(struct qman_thread *t, struct vm_queue *vq,
    struct flow_qman *fqman, uint32_t flow_id,
    uint32_t rate, uint32_t avail, uint16_t max_chunk, uint8_t flags);
/** Actually update queue state for flow queue: must run on queue's home core */
static inline void flow_set_impl(struct qman_thread *t, struct vm_queue *vq,
    struct flow_qman *fqman, 
    uint32_t id, uint32_t rate, uint32_t avail, uint16_t max_chunk, uint8_t flags);
/** Add queue to the flow no limit list */
static inline void flow_queue_activate_nolimit(struct flow_qman *fqman,
    struct flow_queue *q, uint32_t idx);
static inline unsigned flow_poll_nolimit(struct qman_thread *t, 
    struct vm_queue *vqueue, struct flow_qman *fqman, 
    uint32_t cur_ts, unsigned num, unsigned *q_ids, 
    uint16_t *q_bytes, uint32_t *vm_ids, int *bytes_sum);
/** Add queue to the flow skip list list */
static inline void flow_queue_activate_skiplist(struct qman_thread *t,
    struct vm_queue *vq, struct flow_qman *fqman, 
    struct flow_queue *q, uint32_t idx);
static inline unsigned flow_poll_skiplist(struct qman_thread *t, 
    struct vm_queue *vqueue, struct flow_qman *fqman,
    struct skiplist_fstate *skpl_state,
    unsigned num, unsigned *q_ids, 
    uint16_t *q_bytes, uint32_t *vm_ids, int *bytes_sum);
static inline uint8_t flow_queue_level(struct qman_thread *t, 
    struct flow_qman *fqman);
static inline void flow_queue_fire(struct qman_thread *t, 
    struct vm_queue *vqueue, struct flow_qman *fqman,
    struct flow_queue *q, uint32_t idx, unsigned *q_id, 
    uint16_t *q_bytes, uint32_t *vm_ids, int *bytes_sum);
static inline void flow_queue_activate(struct qman_thread *t, struct vm_queue *vq,
    struct flow_qman *fqman, struct flow_queue *q, uint32_t idx);
static inline uint32_t flow_queue_new_ts(struct vm_queue *vq, struct flow_queue *q,
    uint32_t bytes);

/*****************************************************************************/
/* Top level queue manager */

int tas_qman_thread_init(struct dataplane_context *ctx)
{
  struct qman_thread *t = &ctx->qman;

  if (vmcont_init(t) != 0)
  {
    fprintf(stderr, "qman_thread_init: app_cont init failed\n");
    return -1;
  }

  utils_rng_init(&t->rng, RNG_SEED * ctx->id + ctx->id);

  return 0;
}

int tas_qman_poll(struct dataplane_context *ctx, unsigned num, unsigned *vm_ids,
              unsigned *q_ids, uint16_t *q_bytes)
{
  int ret;
  struct skiplist_fstate skpl_state;

  ret = vm_qman_poll(ctx, &skpl_state, num, vm_ids, q_ids, q_bytes);
  return ret;
}

int tas_qman_set(struct qman_thread *t, uint32_t vm_id, uint32_t flow_id, uint32_t rate,
             uint32_t avail, uint16_t max_chunk, uint8_t flags)
{
  int ret;
  ret = vm_qman_set(t, vm_id, flow_id, rate, avail, max_chunk, flags);

  return ret;
}

// TODO: Fix this for multiple VM case. Currently just looking at first VM
uint32_t tas_qman_next_ts(struct qman_thread *t, uint32_t cur_ts)
{
  struct vm_queue *vq;
  struct flow_qman *fqman;
  uint32_t ts = timestamp();
  uint32_t ret_ts;
  struct vm_qman *vqman = t->vqman;

  if (vqman->head_idx == IDXLIST_INVAL)
  {
    return -1;
  }

  vq = &vqman->queues[vqman->head_idx];
  fqman = vq->fqman;

  if (fqman->nolimit_head_idx != IDXLIST_INVAL)
  {
    // Nolimit queue has work - immediate timeout
    fprintf(stderr, "qman nolimit has work\n");
    return 0;
  }

  ret_ts = vq->ts_virtual + (ts - vq->ts_real);
  uint32_t idx = fqman->head_idx[0];
  if (idx != IDXLIST_INVAL)
  {
    struct flow_queue *q = &fqman->queues[idx];

    if (timestamp_lessthaneq(vq, q->next_ts, ret_ts))
    {
      // Fired in the past - immediate timeout
      return 0;
    }
    else
    {
      // Timeout in the future - return difference
      return rel_time(ret_ts, q->next_ts) / 1000;
    }
  }

  // List empty - no timeout
  return -1;
}

uint32_t tas_qman_timestamp(uint64_t cycles)
{
  static uint64_t freq = 0;

  if (freq == 0)
    freq = rte_get_tsc_hz();

  cycles *= 1000000ULL;
  cycles /= freq;
  return cycles;
}

uint32_t timestamp(void)
{
  static uint64_t freq = 0;
  uint64_t cycles = rte_get_tsc_cycles();

  if (freq == 0)
    freq = rte_get_tsc_hz();

  cycles *= 1000000000ULL;
  cycles /= freq;
  return cycles;
}

/** Relative timestamp, ignoring wrap-arounds */
static inline int64_t rel_time(uint32_t cur_ts, uint32_t ts_in)
{
  uint64_t ts = ts_in;
  const uint64_t middle = (1ULL << (TIMESTAMP_BITS - 1));
  uint64_t start, end;

  if (cur_ts < middle)
  {
    /* negative interval is split in half */
    start = (cur_ts - middle) & TIMESTAMP_MASK;
    end = (1ULL << TIMESTAMP_BITS);
    if (start <= ts && ts < end)
    {
      /* in first half of negative interval, smallest timestamps */
      return ts - start - middle;
    }
    else
    {
      /* in second half or in positive interval */
      return ts - cur_ts;
    }
  }
  else if (cur_ts == middle)
  {
    /* intervals not split */
    return ts - cur_ts;
  }
  else
  {
    /* higher interval is split */
    start = 0;
    end = ((cur_ts + middle) & TIMESTAMP_MASK) + 1;
    if (start <= cur_ts && ts < end)
    {
      /* in second half of positive interval, largest timestamps */
      return ts + ((1ULL << TIMESTAMP_BITS) - cur_ts);
    }
    else
    {
      /* in negative interval or first half of positive interval */
      return ts - cur_ts;
    }
  }
}

int timestamp_lessthaneq(struct vm_queue *vq, uint32_t a,
                         uint32_t b)
{
  return rel_time(vq->ts_virtual, a) <= rel_time(vq->ts_virtual, b);
}

/*****************************************************************************/

/*****************************************************************************/
/* Manages vm queues */

int vmcont_init(struct qman_thread *t)
{
  int ret;
  unsigned i;
  struct vm_queue *vq;
  t->vqman = malloc(sizeof(struct vm_qman));
  struct vm_qman *vqman = t->vqman;

  vqman->queues = calloc(1, sizeof(*vqman->queues) * (FLEXNIC_PL_VMST_NUM));
  if (vqman->queues == NULL)
  {
    fprintf(stderr, "vmcont_init: queues malloc failed\n");
    return -1;
  }

  for (i = 0; i < FLEXNIC_PL_VMST_NUM; i++)
  {
    vq = &vqman->queues[i];
    vq->avail = 0;
    vq->id = i;
    vq->ts_virtual = 0;
    vq->ts_real = timestamp();
    ret = flowcont_init(vq);

    if (ret != 0)
    {
      return -1;
    }
  }

  vqman->head_idx = vqman->tail_idx = IDXLIST_INVAL;
  return 0;
}

static inline int vm_qman_poll(struct dataplane_context *ctx,
    struct skiplist_fstate *skpl_state, unsigned num, 
    unsigned *vm_ids, unsigned *q_ids, uint16_t *q_bytes)
{
  uint32_t idx;
  int cnt, temp_cnt, x, bytes_sum;
  struct qman_thread *t = &ctx->qman;
  struct vm_qman *vqman = t->vqman;
  struct flow_qman *fqman;
  struct vm_queue *vq, *rvq;
  
  int oob_n, oob_i = 0;
  struct vm_queue *oob_vms[FLEXNIC_PL_VMST_NUM];
  skpl_state->cur_ts = timestamp();

  rvq = NULL;
  for (cnt = 0; cnt < num && vqman->head_idx != IDXLIST_INVAL;)
  {
    idx = vqman->head_idx;
    vq = &vqman->queues[idx];
    if (rvq == vq)
      break;
   
    vqman->head_idx = vq->next_idx;
    vq->flags &= ~FLAG_INNOLIMITL;
    if (vq->next_idx == IDXLIST_INVAL)
      vqman->tail_idx = IDXLIST_INVAL;

    if (get_budget(idx) > 0)
    {
      fqman = vq->fqman;
      skpl_state->rate_limited = 0;
      skpl_state->orig_num = num; 
      bytes_sum = 0;
      x = flow_qman_poll(t, vq, fqman, skpl_state, num - cnt, 
          q_ids + cnt, q_bytes + cnt, vm_ids + cnt, &bytes_sum);
      cnt += x;

      if (vq->avail > 0)
      {
        vm_queue_fire(vqman, vq, idx, q_bytes, bytes_sum, cnt - x, cnt);
        if (skpl_state->rate_limited && rvq == NULL)
          rvq = vq;
      }

      ctx->vm_counters[idx] += bytes_sum;
      ctx->counters_total += bytes_sum;

    } else
    {
      if (vq->avail > 0)
      {
        oob_vms[oob_i] = vq;
        oob_i++;
      }
    }
  }

  oob_n = oob_i;
  temp_cnt = cnt;
  for (oob_i = 0; oob_i < oob_n; oob_i++)
  {
    vq = oob_vms[oob_i];
    vq->flags &= ~FLAG_INNOLIMITL;

    /** Only serve out of budget VMs if nothing in batch */
    if (cnt < num && temp_cnt == 0)
    {
      fqman = vq->fqman;
      skpl_state->rate_limited = 0;
      bytes_sum = 0;
      x = flow_qman_poll(t, vq, fqman, skpl_state,
          num - cnt, q_ids + cnt, q_bytes + cnt, vm_ids + cnt, 
          &bytes_sum);
      cnt += x;
      if (vq->avail > 0)
      {
        vm_queue_fire(vqman, vq, vq->id, q_bytes, bytes_sum, cnt - x, cnt);
      }
    } else
    {
      vm_queue_activate(vqman, vq, vq->id);
    }
  }

  return cnt;
}

static inline int vm_qman_set(struct qman_thread *t, 
    uint32_t vm_id, uint32_t flow_id, 
    uint32_t rate, uint32_t avail, uint16_t max_chunk, uint8_t flags)
{
  int ret;
  struct vm_qman *vqman = t->vqman;
  struct vm_queue *vq = &vqman->queues[vm_id];
  struct flow_qman *fqman = vq->fqman;

  if (vm_id >= (FLEXNIC_PL_VMST_NUM)) 
  {
    fprintf(stderr, "vm_qman_set: invalid vm id: %u >= %u\n", vm_id,
        FLEXNIC_PL_VMST_NUM);
    return -1;
  }

  vm_set_impl(vqman, vm_id, flow_id, avail, flags);
  ret = flow_qman_set(t, vq, fqman, flow_id, rate, avail, max_chunk, flags);

  return ret;
}

static inline void vm_queue_fire(struct vm_qman *vqman, struct vm_queue *q,
    uint32_t idx, uint16_t *q_bytes, int bytes_sum, 
    unsigned start, unsigned end)
{
  assert(q->avail > 0);

  q->avail -= bytes_sum;

  if (q->avail > 0) {
    vm_queue_activate(vqman, q, idx);
  }

}

static inline void vm_set_impl(struct vm_qman *vqman, uint32_t v_idx,
    uint32_t f_idx, uint32_t avail, uint8_t flags)
{
  struct vm_queue *vq = &vqman->queues[v_idx];
  struct flow_qman *fqman = vq->fqman;
  struct flow_queue *fq = &fqman->queues[f_idx];

  int new_avail = 0;

  if ((flags & QMAN_SET_AVAIL) != 0)
  {
    new_avail = 1;
    int prev_avail = fq->avail;
    vq->avail -= prev_avail;
    vq->avail += avail;
  }
  else if ((flags & QMAN_ADD_AVAIL) != 0)
  {
    vq->avail += avail;
    new_avail = 1;
  }

  if (new_avail && vq->avail > 0 && ((vq->flags & (FLAG_INNOLIMITL)) == 0)) 
  {
    vm_queue_activate(vqman, vq, v_idx);
  }

}

static inline void vm_queue_activate(struct vm_qman *vqman,
    struct vm_queue *q, uint32_t idx)
{
  struct vm_queue *q_tail;

  assert((q->flags & FLAG_INNOLIMITL) == 0);

  q->flags |= FLAG_INNOLIMITL;
  q->next_idx = IDXLIST_INVAL;
  if (vqman->tail_idx == IDXLIST_INVAL)
  {
    vqman->head_idx = vqman->tail_idx = idx;
    return;
  }

  q_tail = &vqman->queues[vqman->tail_idx];
  q_tail->next_idx = idx;
  vqman->tail_idx = idx;
}

/*****************************************************************************/

/*****************************************************************************/
/* Manages flow queues */

int flowcont_init(struct vm_queue *vq) 
{
  unsigned i;
  struct flow_qman *fqman;

  vq->fqman = malloc(sizeof(struct flow_qman));
  fqman = vq->fqman;

  fqman->queues = calloc(1, sizeof(*fqman->queues) * FLEXNIC_NUM_QMFLOWQUEUES);
  if (fqman->queues == NULL)
  {
    fprintf(stderr, "flowcont_init: queues malloc failed\n");
    return -1;
  }

  for (i = 0; i < QMAN_SKIPLIST_LEVELS; i++) 
  {
    fqman->head_idx[i] = IDXLIST_INVAL;
  }
  fqman->nolimit_head_idx = fqman->nolimit_tail_idx = IDXLIST_INVAL;

  return 0;
}

static inline int flow_qman_poll(struct qman_thread *t, struct vm_queue *vqueue, 
    struct flow_qman *fqman, struct skiplist_fstate *skpl_state,
    unsigned num, unsigned *q_ids, uint16_t *q_bytes, uint32_t *vm_ids,
    int *bytes_sum)

{
  unsigned x, y;
  /* poll nolimit list and skiplist alternating the order between */
  if (fqman->nolimit_first) {
    x = flow_poll_nolimit(t, vqueue, fqman, skpl_state->cur_ts, 
        num, q_ids, q_bytes, vm_ids, bytes_sum);
    y = flow_poll_skiplist(t, vqueue, fqman, skpl_state,
        num - x, q_ids + x, q_bytes + x, vm_ids + x, bytes_sum);
  } else {
    x = flow_poll_skiplist(t, vqueue, fqman, skpl_state,
        num, q_ids, q_bytes, vm_ids, bytes_sum);
    y = flow_poll_nolimit(t, vqueue, fqman, skpl_state->cur_ts, 
        num - x, q_ids + x, q_bytes + x, vm_ids + x, bytes_sum);
  }
  fqman->nolimit_first = !fqman->nolimit_first;

  return x + y;
}

int flow_qman_set(struct qman_thread *t, struct vm_queue *vq, 
    struct flow_qman *fqman, uint32_t id, 
    uint32_t rate, uint32_t avail, uint16_t max_chunk, uint8_t flags)
{
#ifdef FLEXNIC_TRACE_QMAN
  struct flexnic_trace_entry_qman_set evt = {
      .id = id, .rate = rate, .avail = avail, .max_chunk = max_chunk,
      .flags = flags,
    };
  trace_event(FLEXNIC_TRACE_EV_QMSET, sizeof(evt), &evt);
#endif

  dprintf("flow_qman_set: id=%u rate=%u avail=%u max_chunk=%u\n",
      id, rate, avail, max_chunk);

  if (id >= FLEXNIC_NUM_QMFLOWQUEUES) {
    fprintf(stderr, "flow_qman_set: invalid queue id: %u >= %u\n", id,
        FLEXNIC_NUM_QMFLOWQUEUES);
    return -1;
  }

  flow_set_impl(t, vq, fqman, id, rate, avail, max_chunk, flags);

  return 0;
}

/** Actually update queue state: must run on queue's home core */
static void inline flow_set_impl(struct qman_thread *t, 
    struct vm_queue *vq, struct flow_qman *fqman, 
    uint32_t idx, uint32_t rate, uint32_t avail, uint16_t max_chunk, uint8_t flags)
{
  struct flow_queue *q = &fqman->queues[idx];
  int new_avail = 0;

  if ((flags & QMAN_SET_RATE) != 0) {
    q->rate = rate;
  }

  if ((flags & QMAN_SET_MAXCHUNK) != 0) {
    q->max_chunk = max_chunk;
  }

  if ((flags & QMAN_SET_AVAIL) != 0) {
    q->avail = avail;
    new_avail = 1;
  } else if ((flags & QMAN_ADD_AVAIL) != 0) {
    q->avail += avail;
    new_avail = 1;
  }

  dprintf("flow_set_impl: t=%p q=%p idx=%u avail=%u rate=%u qflags=%x flags=%x\n", 
      t, q, idx, q->avail, q->rate, q->flags, flags);

  if (new_avail && q->avail > 0
      && ((q->flags & (FLAG_INSKIPLIST | FLAG_INNOLIMITL)) == 0)) {
    flow_queue_activate(t, vq, fqman, q, idx);
  }
}

/** Add queue to the no limit list for flows */
static inline void flow_queue_activate_nolimit(struct flow_qman *fqman,
    struct flow_queue *q, uint32_t idx)
{
  struct flow_queue *q_tail;

  assert((q->flags & (FLAG_INSKIPLIST | FLAG_INNOLIMITL)) == 0);

  dprintf("flow_queue_activate_nolimit: q=%p avail=%u rate=%u flags=%x\n",
      q, q->avail, q->rate, q->flags);

  q->flags |= FLAG_INNOLIMITL;
  q->next_idxs[0] = IDXLIST_INVAL;
  if (fqman->nolimit_tail_idx == IDXLIST_INVAL) 
  {
    fqman->nolimit_head_idx = fqman->nolimit_tail_idx = idx;
    return;
  }

  q_tail = &fqman->queues[fqman->nolimit_tail_idx];
  q_tail->next_idxs[0] = idx;
  fqman->nolimit_tail_idx = idx;
}

/** Poll no-limit queues for flows */
static inline unsigned flow_poll_nolimit(struct qman_thread *t, 
    struct vm_queue *vqueue, struct flow_qman *fqman, 
    uint32_t cur_ts, unsigned num, unsigned *q_ids, 
    uint16_t *q_bytes, uint32_t *vm_ids, int *bytes_sum)
{
  unsigned cnt;
  struct flow_queue *q;
  uint32_t idx;

  for (cnt = 0; cnt < num && fqman->nolimit_head_idx != IDXLIST_INVAL;) {
    idx = fqman->nolimit_head_idx;
    q = fqman->queues + idx;

    fqman->nolimit_head_idx = q->next_idxs[0];
    if (q->next_idxs[0] == IDXLIST_INVAL)
      fqman->nolimit_tail_idx = IDXLIST_INVAL;

    q->flags &= ~FLAG_INNOLIMITL;
    dprintf("flow_poll_nolimit: t=%p q=%p idx=%u avail=%u rate=%u flags=%x\n",
        t, q, idx, q->avail, q->rate, q->flags);
    if (q->avail > 0) {
      flow_queue_fire(t, vqueue, fqman, q, idx, q_ids + cnt, 
          q_bytes + cnt, vm_ids + cnt, bytes_sum);
      cnt++;
    }
  }

  return cnt;
}

/** Add queue to the flows skip list */
static inline void flow_queue_activate_skiplist(struct qman_thread *t, 
    struct vm_queue *vq, struct flow_qman *fqman,
    struct flow_queue *q, uint32_t q_idx)
{
  uint8_t level;
  int8_t l;
  uint32_t preds[QMAN_SKIPLIST_LEVELS];
  uint32_t pred, idx, ts, max_ts;

  assert((q->flags & (FLAG_INSKIPLIST | FLAG_INNOLIMITL)) == 0);

  dprintf("flow_queue_activate_skiplist: t=%p q=%p idx=%u avail=%u "
      "rate=%u flags=%x ts_virt=%u next_ts=%u\n", 
      t, q, q_idx, q->avail, q->rate, q->flags, t->ts_virtual, q->next_ts);

  /* make sure queue has a reasonable next_ts:
   *  - not in the past
   *  - not more than if it just sent max_chunk at the current rate
   */
  ts = q->next_ts;
  max_ts = flow_queue_new_ts(vq, q, q->max_chunk);
  if (timestamp_lessthaneq(vq, ts, vq->ts_virtual)) {
    ts = q->next_ts = vq->ts_virtual;
  } else if (!timestamp_lessthaneq(vq, ts, max_ts)) {
    ts = q->next_ts = max_ts;
  }

  q->next_ts = ts;

  /* find predecessors at all levels top-down */
  pred = IDXLIST_INVAL;
  for (l = QMAN_SKIPLIST_LEVELS - 1; l >= 0; l--) {
    idx = (pred != IDXLIST_INVAL ? pred : fqman->head_idx[l]);
    while (idx != IDXLIST_INVAL &&
        timestamp_lessthaneq(vq, fqman->queues[idx].next_ts, ts))
    {
      pred = idx;
      idx = fqman->queues[idx].next_idxs[l];
    }
    preds[l] = pred;
    dprintf("    pred[%u] = %d\n", l, pred);
  }

  /* determine level for this queue */
  level = flow_queue_level(t, fqman);
  dprintf("    level = %u\n", level);

  /* insert into skip-list */
  for (l = QMAN_SKIPLIST_LEVELS - 1; l >= 0; l--) {
    if (l > level) {
      q->next_idxs[l] = IDXLIST_INVAL;
    } else {
      idx = preds[l];
      if (idx != IDXLIST_INVAL) {
        q->next_idxs[l] = fqman->queues[idx].next_idxs[l];
        fqman->queues[idx].next_idxs[l] = q_idx;
      } else {
        q->next_idxs[l] = fqman->head_idx[l];
        fqman->head_idx[l] = q_idx;
      }
    }
  }

  q->flags |= FLAG_INSKIPLIST;
}

/** Poll skiplist queues for flows */
static inline unsigned flow_poll_skiplist(struct qman_thread *t, 
    struct vm_queue *vqueue, struct flow_qman *fqman,
    struct skiplist_fstate *skpl_state, unsigned num, 
    unsigned *q_ids, uint16_t *q_bytes, uint32_t *vm_ids,
    int *bytes_sum)
{
  unsigned cnt;
  uint32_t idx, max_vts;
  int8_t l;
  struct flow_queue *q;

  /* maximum virtual time stamp that can be reached */
  max_vts = vqueue->ts_virtual + (skpl_state->cur_ts - vqueue->ts_real);

  for (cnt = 0; cnt < num;) {
    idx = fqman->head_idx[0];
    q = &fqman->queues[idx];

    /* no more queues */
    if (idx == IDXLIST_INVAL) {
      vqueue->ts_virtual = max_vts;
      break;
    }

    /* beyond max_vts */
    dprintf("flow_poll_skiplist: next_ts=%u vts=%u rts=%u max_vts=%u cur_ts=%u\n",
        q->next_ts, vqueue->ts_virtual, vqueue->ts_real, max_vts, skpl_state->cur_ts);
    if (!timestamp_lessthaneq(vqueue, q->next_ts, max_vts)) {
      vqueue->ts_virtual = max_vts;
      skpl_state->rate_limited = 1;
      break;
    }

    /* remove queue from skiplist */
    for (l = 0; l < QMAN_SKIPLIST_LEVELS && fqman->head_idx[l] == idx; l++) {
      fqman->head_idx[l] = q->next_idxs[l];
    }
    assert((q->flags & FLAG_INSKIPLIST) != 0);
    q->flags &= ~FLAG_INSKIPLIST;

    /* advance virtual timestamp */
    vqueue->ts_virtual = q->next_ts;

    dprintf("flow_poll_skiplist: t=%p q=%p idx=%u avail=%u rate=%u flags=%x\n",
        t, q, idx, q->avail, q->rate, q->flags);

    if (q->avail > 0) {
      flow_queue_fire(t, vqueue, fqman, q, idx, q_ids + cnt, 
          q_bytes + cnt, vm_ids + cnt, bytes_sum);
      cnt++;
    }

  }

  /* if we reached the limit, update the virtual timestamp correctly */
  if (cnt == skpl_state->orig_num) {
    idx = fqman->head_idx[0];
    if (idx != IDXLIST_INVAL &&
        timestamp_lessthaneq(vqueue, fqman->queues[idx].next_ts, max_vts))
    {
      vqueue->ts_virtual = fqman->queues[idx].next_ts;
    } else 
    {
      vqueue->ts_virtual = max_vts;
    }
  }

  vqueue->ts_real = skpl_state->cur_ts;
  return cnt;
}

static inline uint32_t flow_queue_new_ts(struct vm_queue *vq, struct flow_queue *q,
    uint32_t bytes)
{
  return vq->ts_virtual + ((uint64_t) bytes * 8 * 1000000) / q->rate;
}

/** Level for queue added to skiplist for flows*/
static inline uint8_t flow_queue_level(struct qman_thread *t, struct flow_qman *fqman)
{
  uint8_t x = (__builtin_ffs(utils_rng_gen32(&t->rng)) - 1) / SKIPLIST_BITS;
  return (x < QMAN_SKIPLIST_LEVELS ? x : QMAN_SKIPLIST_LEVELS - 1);
}

static inline void flow_queue_fire(struct qman_thread *t,
    struct vm_queue *vqueue, struct flow_qman *fqman, 
    struct flow_queue *q, uint32_t idx, unsigned *q_id, 
    uint16_t *q_bytes, uint32_t *vm_ids, int *bytes_sum)
{
  uint32_t bytes;

  assert(q->avail > 0);

  bytes = (q->avail <= q->max_chunk ? q->avail : q->max_chunk);
  q->avail -= bytes;

  dprintf("flow_queue_fire: t=%p q=%p idx=%u gidx=%u bytes=%u avail=%u rate=%u\n",
      t, q, idx, idx, bytes, q->avail, q->rate);
  if (q->rate > 0) 
  {
    q->next_ts = flow_queue_new_ts(vqueue, q, bytes);
  }

  if (q->avail > 0) 
  {
    flow_queue_activate(t, vqueue, fqman, q, idx);
  }

  *bytes_sum += bytes;
  *q_bytes = bytes;
  *q_id = idx;
  *vm_ids = vqueue->id;

#ifdef FLEXNIC_TRACE_QMAN
  struct flexnic_trace_entry_qman_event evt = {
      .id = *q_id, .bytes = bytes,
    };
  trace_event(FLEXNIC_TRACE_EV_QMEVT, sizeof(evt), &evt);
#endif

}

static inline void flow_queue_activate(struct qman_thread *t, struct vm_queue *vq,
    struct flow_qman *fqman, struct flow_queue *q, uint32_t idx)
{
  if (q->rate == 0) {
    flow_queue_activate_nolimit(fqman, q, idx);
  } else {
    flow_queue_activate_skiplist(t, vq, fqman, q, idx);
  }
}

/*****************************************************************************/

/*****************************************************************************/
/* Helper functions for unit tests */

void qman_free_vm_cont(struct dataplane_context *ctx)
{
  int i;
  struct vm_qman *vqman;
  struct vm_queue *vq;
  struct flow_qman *fqman;

  vqman = ctx->qman.vqman;

  for (i = 0; i < FLEXNIC_PL_VMST_NUM; i++)
  {
    vq = &vqman->queues[i];
    fqman = vq->fqman;
    free(fqman->queues);
    free(fqman);
  }

  free(vqman->queues);
  free(vqman);
}

uint32_t qman_vm_get_avail(struct dataplane_context *ctx, uint32_t vm_id)
{
  uint32_t avail;
  struct vm_qman *vqman;
  struct vm_queue *vq;
  
  vqman = ctx->qman.vqman;
  vq = &vqman->queues[vm_id];
  avail = vq->avail;
  
  return avail;
}