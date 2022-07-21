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

#include "internal.h"

/* TODO: Retrieve packets from more than one app with each call to qman_poll */
/* TODO: Add rate to app containers */
/* TODO: Make sure flow_id makes sense when distributed among different apps.
   find way to translate absolute flow_id to an id in the application queue.
   Right now, we are using more memory than necessary to keep track of all
   the queues. */

#define dprintf(...) do { } while (0)

#define FLAG_INSKIPLIST 1
#define FLAG_INNOLIMITL 2

/** Skiplist: bits per level */
#define SKIPLIST_BITS 3
/** Index list: invalid index */
#define IDXLIST_INVAL (-1U)

#define RNG_SEED 0x12345678
#define TIMESTAMP_BITS 32
#define TIMESTAMP_MASK 0xFFFFFFFF

/** Queue container for an application */
struct app_cont {
  /** Application queue */
  struct app_queue *queues;
  /** Idx of head of queue */
  uint32_t head_idx;
  /** Idx of tail of queue */
  uint32_t tail_idx;
};

/** Queue container for a flow **/
struct flow_cont {
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

/** Queue state for application */
struct app_queue {
  /** Next pointer */
  uint32_t next_idx;
  /** Pointer to container with flows for this app */
  struct flow_cont *f_cont;
  /** Number of entries for this app */
  uint32_t avail;
  /** Flags: FLAG_INNOLIMITL */
  uint16_t flags;
  /* Deficit counter */
  uint16_t dc;
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
static inline uint32_t sum_bytes(uint16_t *q_bytes, unsigned start, unsigned end);
static inline uint32_t timestamp(void);
static inline int timestamp_lessthaneq(struct qman_thread *t, uint32_t a,
    uint32_t b);

/** Qman functions for app */
static inline int appcont_init(struct qman_thread *t);
static inline int app_qman_poll(struct qman_thread *t, struct app_cont *ac,
    unsigned num, unsigned *app_id, unsigned *q_ids, uint16_t *q_bytes);
int app_qman_set(struct qman_thread *t, uint32_t app_id, uint32_t flow_id,
    uint32_t rate, uint32_t avail, uint16_t max_chunk, uint8_t flags);
static inline void app_queue_fire(struct app_cont *ac, struct app_queue *q,
    uint32_t idx, uint16_t *q_bytes, unsigned start, unsigned end);
/** Actually update queue state for app queue */
static inline void app_set_impl(struct app_cont *ac, uint32_t a_idx,
    uint32_t f_idx, uint32_t avail, uint8_t flags);
static inline void app_queue_activate(struct app_cont *ac,
    struct app_queue *q, uint32_t idx);

/** Qman management functions for flows */
static inline int flowcont_init(struct app_queue *aq);
static inline int flow_qman_poll(struct qman_thread *t, struct flow_cont *fc,
    unsigned num, unsigned *q_ids, uint16_t *q_bytes);
int flow_qman_set(struct qman_thread *t, struct flow_cont *fc, uint32_t flow_id,
    uint32_t rate, uint32_t avail, uint16_t max_chunk, uint8_t flags);
/** Actually update queue state for flow queue: must run on queue's home core */
static inline void flow_set_impl(struct qman_thread *t, struct flow_cont *fc, 
    uint32_t id, uint32_t rate, uint32_t avail, uint16_t max_chunk, uint8_t flags);
/** Add queue to the flow no limit list */
static inline void flow_queue_activate_nolimit(struct flow_cont *fc,
    struct flow_queue *q, uint32_t idx);
static inline unsigned flow_poll_nolimit(struct qman_thread *t, struct flow_cont *fc, 
    uint32_t cur_ts, unsigned num, unsigned *q_ids, uint16_t *q_bytes);
/** Add queue to the flow skip list list */
static inline void flow_queue_activate_skiplist(struct qman_thread *t,
    struct flow_cont *fc, struct flow_queue *q, uint32_t idx);
static inline unsigned flow_poll_skiplist(struct qman_thread *t, struct flow_cont *fc,
    uint32_t cur_ts, unsigned num, unsigned *q_ids, uint16_t *q_bytes);
static inline uint8_t flow_queue_level(struct qman_thread *t, 
    struct flow_cont *fc);
static inline void flow_queue_fire(struct qman_thread *t, struct flow_cont *fc,
    struct flow_queue *q, uint32_t idx, unsigned *q_id, uint16_t *q_bytes);
static inline void flow_queue_activate(struct qman_thread *t, struct flow_cont *fc, 
    struct flow_queue *q, uint32_t idx);
static inline uint32_t flow_queue_new_ts(struct qman_thread *t, struct flow_queue *q,
    uint32_t bytes);


/*****************************************************************************/
/* Top level queue manager */

int qman_thread_init(struct dataplane_context *ctx)
{
  struct qman_thread *t = &ctx->qman;

  if (appcont_init(t) != 0)
  {
    fprintf(stderr, "qman_thread_init: app_cont init failed\n");
    return -1;
  }

  utils_rng_init(&t->rng, RNG_SEED * ctx->id + ctx->id);
  t->ts_virtual = 0;
  t->ts_real = timestamp();
  return 0;
}

int qman_poll(struct qman_thread *t, unsigned num, unsigned *app_id,
              unsigned *q_ids, uint16_t *q_bytes)
{
  int ret;
  struct app_cont *ac = t->a_cont;

  ret = app_qman_poll(t, ac, num, app_id, q_ids, q_bytes);
  return ret;
}

int qman_set(struct qman_thread *t, uint32_t app_id, uint32_t flow_id, uint32_t rate,
             uint32_t avail, uint16_t max_chunk, uint8_t flags)
{
  int ret;
  ret = app_qman_set(t, app_id, flow_id, rate, avail, max_chunk, flags);
  return ret;
}

uint32_t qman_next_ts(struct qman_thread *t, uint32_t cur_ts)
{
  struct app_queue *aq;
  struct flow_cont *fc;
  uint32_t ts = timestamp();
  uint32_t ret_ts = t->ts_virtual + (ts - t->ts_real);
  struct app_cont *ac = t->a_cont;

  if (ac->head_idx == IDXLIST_INVAL)
  {
    return -1;
  }

  aq = &ac->queues[ac->head_idx];
  fc = aq->f_cont;

  if (fc->nolimit_head_idx != IDXLIST_INVAL)
  {
    // Nolimit queue has work - immediate timeout
    fprintf(stderr, "QMan nolimit has work\n");
    return 0;
  }

  uint32_t idx = fc->head_idx[0];
  if (idx != IDXLIST_INVAL)
  {
    struct flow_queue *q = &fc->queues[idx];

    if (timestamp_lessthaneq(t, q->next_ts, ret_ts))
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

uint32_t qman_timestamp(uint64_t cycles)
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

int timestamp_lessthaneq(struct qman_thread *t, uint32_t a,
                         uint32_t b)
{
  return rel_time(t->ts_virtual, a) <= rel_time(t->ts_virtual, b);
}

/*****************************************************************************/

/*****************************************************************************/
/* Manages application queues */

int appcont_init(struct qman_thread *t)
{
  int ret;
  unsigned i;
  struct app_queue *aq;
  t->a_cont = malloc(sizeof(struct app_cont));
  struct app_cont *ac = t->a_cont;

  ac->queues = calloc(1, sizeof(*ac->queues) * FLEXNIC_PL_APPST_NUM);
  if (ac->queues == NULL)
  {
    fprintf(stderr, "appcont_init: queues malloc failed\n");
    return -1;
  }

  for (i = 0; i < FLEXNIC_PL_APPST_NUM; i++)
  {
    aq = &ac->queues[i];
    aq->avail = 0;
    aq->dc = BATCH_SIZE;
    ret = flowcont_init(aq);

    if (ret != 0)
    {
      return -1;
    }
  }

  ac->head_idx = ac->tail_idx = IDXLIST_INVAL;
  return 0;
}

int app_qman_poll(struct qman_thread *t, struct app_cont *ac, unsigned num, 
    unsigned *app_id, unsigned *q_ids, uint16_t *q_bytes)
{
  int i, cnt, x;
  // uint16_t quanta = 2;
  uint32_t idx;

  for (cnt = 0; cnt < num && ac->head_idx != IDXLIST_INVAL;)
  {
    idx = ac->head_idx;
    struct app_queue *aq = &ac->queues[idx];
    struct flow_cont *fc = aq->f_cont;

    ac->head_idx = aq->next_idx;
    if (aq->next_idx == IDXLIST_INVAL)
    {
      ac->tail_idx = IDXLIST_INVAL;
    }

    aq->flags &= ~FLAG_INNOLIMITL;

    // if (aq->dc < num - cnt)
    // {
    //   x = flow_qman_poll(t, fc, aq->dc, q_ids + cnt, q_bytes + cnt);
    // }
    // else
    // {
    x = flow_qman_poll(t, fc, num - cnt, q_ids + cnt, q_bytes + cnt);
    // }

    // aq->dc -= x;
    cnt += x;

    // Update app_id list
    for (i = cnt - x; i < cnt; i++)
    {
      app_id[i] = idx;
    }

    if (aq->avail > 0)
    {
      app_queue_fire(ac, aq, idx, q_bytes, cnt - x, cnt);
    }
  
    // if ((aq->dc + quanta) > BATCH_SIZE)
    // {
    //   aq->dc = BATCH_SIZE;
    // }
    // else{
    //   aq->dc += quanta;
    // }

  }


  return cnt;
}

int app_qman_set(struct qman_thread *t, uint32_t app_id, uint32_t flow_id, 
    uint32_t rate, uint32_t avail, uint16_t max_chunk, uint8_t flags)
{
  int ret;
  struct app_cont *ac = t->a_cont;
  struct app_queue *aq = &ac->queues[app_id];
  struct flow_cont *fc = aq->f_cont;

  if (app_id >= FLEXNIC_PL_APPST_NUM) 
  {
    fprintf(stderr, "app_qman_set: invalid queue id: %u >= %u\n", app_id,
        FLEXNIC_PL_APPST_NUM);
    return -1;
  }

  app_set_impl(ac, app_id, flow_id, avail, flags);
  ret = flow_qman_set(t, fc, flow_id, rate, avail, max_chunk, flags);

  return ret;
}

static inline void app_queue_fire(struct app_cont *ac, struct app_queue *q,
    uint32_t idx, uint16_t *q_bytes, unsigned start, unsigned end)
{
  uint32_t bytes;
  assert(q->avail > 0);

  bytes = sum_bytes(q_bytes, start, end);
  q->avail -= bytes;

  if (q->avail > 0) {
    app_queue_activate(ac, q, idx);
  }

}

static inline void app_set_impl(struct app_cont *ac, uint32_t a_idx,
    uint32_t f_idx, uint32_t avail, uint8_t flags)
{
  struct app_queue *aq = &ac->queues[a_idx];
  struct flow_cont *fc = aq->f_cont;
  struct flow_queue *fq = &fc->queues[f_idx];

  int new_avail = 0;

  if ((flags & QMAN_SET_AVAIL) != 0)
  {
    new_avail = 1;
    int prev_avail = fq->avail;
    aq->avail -= prev_avail;
    aq->avail += avail;
  }
  else if ((flags & QMAN_ADD_AVAIL) != 0)
  {
    aq->avail += avail;
    new_avail = 1;
  }

  if (new_avail && aq->avail > 0 && ((aq->flags & (FLAG_INNOLIMITL)) == 0)) 
  {
    app_queue_activate(ac, aq, a_idx);
  }

}

static inline void app_queue_activate(struct app_cont *ac,
    struct app_queue *q, uint32_t idx)
{
  struct app_queue *q_tail;

  assert((q->flags & FLAG_INNOLIMITL) == 0);

  q->flags |= FLAG_INNOLIMITL;
  q->next_idx = IDXLIST_INVAL;
  if (ac->tail_idx == IDXLIST_INVAL)
  {
    ac->head_idx = ac->tail_idx = idx;
    return;
  }

  q_tail = &ac->queues[ac->tail_idx];
  q_tail->next_idx = idx;
  ac->tail_idx = idx;
}

static inline uint32_t sum_bytes(uint16_t *q_bytes, unsigned start, unsigned end)
{
  int i;
  uint32_t bytes = 0;
  for (i = start; i < end; i++)
  {
    bytes += q_bytes[i];
  }

  return bytes;
}

/*****************************************************************************/

/*****************************************************************************/
/* Manages flow queues */

int flowcont_init(struct app_queue *aq) 
{
  unsigned i;
  struct flow_cont *fc;

  aq->f_cont = malloc(sizeof(struct flow_cont));
  fc = aq->f_cont;

  fc->queues = calloc(1, sizeof(*fc->queues) * FLEXNIC_NUM_QMFLOWQUEUES);
  if (fc->queues == NULL)
  {
    fprintf(stderr, "flowcont_init: queues malloc failed\n");
    return -1;
  }

  for (i = 0; i < QMAN_SKIPLIST_LEVELS; i++) 
  {
    fc->head_idx[i] = IDXLIST_INVAL;
  }
  fc->nolimit_head_idx = fc->nolimit_tail_idx = IDXLIST_INVAL;

  return 0;
}

int flow_qman_poll(struct qman_thread *t, struct flow_cont *fc, unsigned num, 
    unsigned *q_ids, uint16_t *q_bytes)
{
  unsigned x, y;
  uint32_t ts = timestamp();

  /* poll nolimit list and skiplist alternating the order between */
  if (fc->nolimit_first) {
    x = flow_poll_nolimit(t, fc, ts, num, q_ids, q_bytes);
    y = flow_poll_skiplist(t, fc, ts, num - x, q_ids + x, q_bytes + x);
  } else {
    x = flow_poll_skiplist(t, fc, ts, num, q_ids, q_bytes);
    y = flow_poll_nolimit(t, fc, ts, num - x, q_ids + x, q_bytes + x);
  }
  fc->nolimit_first = !fc->nolimit_first;

  return x + y;
}

int flow_qman_set(struct qman_thread *t, struct flow_cont *fc, uint32_t id, 
    uint32_t rate, uint32_t avail, uint16_t max_chunk, uint8_t flags)
{
#ifdef FLEXNIC_TRACE_QMAN
  struct flexnic_trace_entry_qman_set evt = {
      .id = id, .rate = rate, .avail = avail, .max_chunk = max_chunk,
      .flags = flags,
    };
  trace_event(FLEXNIC_TRACE_EV_QMSET, sizeof(evt), &evt);
#endif

  dprintf("flow_qman_set: id=%u rate=%u avail=%u max_chunk=%u qidx=%u tid=%u\n",
      id, rate, avail, max_chunk, qidx, tid);

  if (id >= FLEXNIC_NUM_QMFLOWQUEUES) {
    fprintf(stderr, "flow_qman_set: invalid queue id: %u >= %u\n", id,
        FLEXNIC_NUM_QMFLOWQUEUES);
    return -1;
  }

  flow_set_impl(t, fc, id, rate, avail, max_chunk, flags);

  return 0;
}

/** Actually update queue state: must run on queue's home core */
static void inline flow_set_impl(struct qman_thread *t, struct flow_cont *fc, 
    uint32_t idx, uint32_t rate, uint32_t avail, uint16_t max_chunk, uint8_t flags)
{
  struct flow_queue *q = &fc->queues[idx];
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

  dprintf("flow_set_impl: t=%p q=%p idx=%u avail=%u rate=%u qflags=%x flags=%x\n", t, q, idx, q->avail, q->rate, q->flags, flags);

  if (new_avail && q->avail > 0
      && ((q->flags & (FLAG_INSKIPLIST | FLAG_INNOLIMITL)) == 0)) {
    flow_queue_activate(t, fc, q, idx);
  }
}

/** Add queue to the no limit list for flows */
static inline void flow_queue_activate_nolimit(struct flow_cont *fc,
    struct flow_queue *q, uint32_t idx)
{
  struct flow_queue *q_tail;

  assert((q->flags & (FLAG_INSKIPLIST | FLAG_INNOLIMITL)) == 0);

  dprintf("flow_queue_activate_nolimit: t=%p q=%p avail=%u rate=%u flags=%x\n", t, q, q->avail, q->rate, q->flags);

  q->flags |= FLAG_INNOLIMITL;
  q->next_idxs[0] = IDXLIST_INVAL;
  if (fc->nolimit_tail_idx == IDXLIST_INVAL) 
  {
    fc->nolimit_head_idx = fc->nolimit_tail_idx = idx;
    return;
  }

  q_tail = &fc->queues[fc->nolimit_tail_idx];
  q_tail->next_idxs[0] = idx;
  fc->nolimit_tail_idx = idx;
}

/** Poll no-limit queues for flows */
static inline unsigned flow_poll_nolimit(struct qman_thread *t, struct flow_cont *fc,
    uint32_t cur_ts, unsigned num, unsigned *q_ids, uint16_t *q_bytes)
{
  unsigned cnt;
  struct flow_queue *q;
  uint32_t idx;

  for (cnt = 0; cnt < num && fc->nolimit_head_idx != IDXLIST_INVAL;) {
    idx = fc->nolimit_head_idx;
    q = fc->queues + idx;

    fc->nolimit_head_idx = q->next_idxs[0];
    if (q->next_idxs[0] == IDXLIST_INVAL)
      fc->nolimit_tail_idx = IDXLIST_INVAL;

    q->flags &= ~FLAG_INNOLIMITL;
    dprintf("flow_poll_nolimit: t=%p q=%p idx=%u avail=%u rate=%u flags=%x\n", t, q, idx, q->avail, q->rate, q->flags);
    if (q->avail > 0) {
      flow_queue_fire(t, fc, q, idx, q_ids + cnt, q_bytes + cnt);
      cnt++;
    }
  }

  return cnt;
}

/** Add queue to the flows skip list */
static inline void flow_queue_activate_skiplist(struct qman_thread *t, 
    struct flow_cont *fc, struct flow_queue *q, uint32_t q_idx)
{
  uint8_t level;
  int8_t l;
  uint32_t preds[QMAN_SKIPLIST_LEVELS];
  uint32_t pred, idx, ts, max_ts;

  assert((q->flags & (FLAG_INSKIPLIST | FLAG_INNOLIMITL)) == 0);

  dprintf("flow_queue_activate_skiplist: t=%p q=%p idx=%u avail=%u rate=%u flags=%x ts_virt=%u next_ts=%u\n", t, q, q_idx, q->avail, q->rate, q->flags,
      t->ts_virtual, q->next_ts);

  /* make sure queue has a reasonable next_ts:
   *  - not in the past
   *  - not more than if it just sent max_chunk at the current rate
   */
  ts = q->next_ts;
  max_ts = flow_queue_new_ts(t, q, q->max_chunk);
  if (timestamp_lessthaneq(t, ts, t->ts_virtual)) {
    ts = q->next_ts = t->ts_virtual;
  } else if (!timestamp_lessthaneq(t, ts, max_ts)) {
    ts = q->next_ts = max_ts;
  }
  q->next_ts = ts;

  /* find predecessors at all levels top-down */
  pred = IDXLIST_INVAL;
  for (l = QMAN_SKIPLIST_LEVELS - 1; l >= 0; l--) {
    idx = (pred != IDXLIST_INVAL ? pred : fc->head_idx[l]);
    while (idx != IDXLIST_INVAL &&
        timestamp_lessthaneq(t, fc->queues[idx].next_ts, ts))
    {
      
      pred = idx;
      idx = fc->queues[idx].next_idxs[l];
    }
    preds[l] = pred;
    dprintf("    pred[%u] = %d\n", l, pred);
  }

  /* determine level for this queue */
  level = flow_queue_level(t, fc);
  dprintf("    level = %u\n", level);

  /* insert into skip-list */
  for (l = QMAN_SKIPLIST_LEVELS - 1; l >= 0; l--) {
    if (l > level) {
      q->next_idxs[l] = IDXLIST_INVAL;
    } else {
      idx = preds[l];
      if (idx != IDXLIST_INVAL) {
        q->next_idxs[l] = fc->queues[idx].next_idxs[l];
        fc->queues[idx].next_idxs[l] = q_idx;
      } else {
        q->next_idxs[l] = fc->head_idx[l];
        fc->head_idx[l] = q_idx;
      }
    }
  }

  q->flags |= FLAG_INSKIPLIST;
}

/** Poll skiplist queues for flows */
static inline unsigned flow_poll_skiplist(struct qman_thread *t, struct flow_cont *fc,
    uint32_t cur_ts, unsigned num, unsigned *q_ids, uint16_t *q_bytes)
{
  unsigned cnt;
  uint32_t idx, max_vts;
  int8_t l;
  struct flow_queue *q;

  /* maximum virtual time stamp that can be reached */
  max_vts = t->ts_virtual + (cur_ts - t->ts_real);

  for (cnt = 0; cnt < num;) {
    idx = fc->head_idx[0];

    /* no more queues */
    if (idx == IDXLIST_INVAL) {
      t->ts_virtual = max_vts;
      break;
    }

    q = &fc->queues[idx];

    /* beyond max_vts */
    dprintf("flow_poll_skiplist: next_ts=%u vts=%u rts=%u max_vts=%u cur_ts=%u\n",
        q->next_ts, t->ts_virtual, t->ts_real, max_vts, cur_ts);
    if (!timestamp_lessthaneq(t, q->next_ts, max_vts)) {
      t->ts_virtual = max_vts;
      break;
    }

    /* remove queue from skiplist */
    for (l = 0; l < QMAN_SKIPLIST_LEVELS && fc->head_idx[l] == idx; l++) {
      fc->head_idx[l] = q->next_idxs[l];
    }
    assert((q->flags & FLAG_INSKIPLIST) != 0);
    q->flags &= ~FLAG_INSKIPLIST;

    /* advance virtual timestamp */
    t->ts_virtual = q->next_ts;

    dprintf("flow_poll_skiplist: t=%p q=%p idx=%u avail=%u rate=%u flags=%x\n", t, q, idx, q->avail, q->rate, q->flags);

    if (q->avail > 0) {
      flow_queue_fire(t, fc, q, idx, q_ids + cnt, q_bytes + cnt);
      cnt++;
    }
  }

  /* if we reached the limit, update the virtual timestamp correctly */
  if (cnt == num) {
    idx = fc->head_idx[0];
    if (idx != IDXLIST_INVAL &&
        timestamp_lessthaneq(t, fc->queues[idx].next_ts, max_vts))
    {
      t->ts_virtual = fc->queues[idx].next_ts;
    } else 
    {
      t->ts_virtual = max_vts;
    }
  }

  t->ts_real = cur_ts;
  return cnt;
}

static inline uint32_t flow_queue_new_ts(struct qman_thread *t, struct flow_queue *q,
    uint32_t bytes)
{
  return t->ts_virtual + ((uint64_t) bytes * 8 * 1000000) / q->rate;
}

/** Level for queue added to skiplist for flows*/
static inline uint8_t flow_queue_level(struct qman_thread *t, struct flow_cont *fc)
{
  uint8_t x = (__builtin_ffs(utils_rng_gen32(&t->rng)) - 1) / SKIPLIST_BITS;
  return (x < QMAN_SKIPLIST_LEVELS ? x : QMAN_SKIPLIST_LEVELS - 1);
}

static inline void flow_queue_fire(struct qman_thread *t, struct flow_cont *fc,
    struct flow_queue *q, uint32_t idx, unsigned *q_id, uint16_t *q_bytes)
{
  uint32_t bytes;

  assert(q->avail > 0);

  bytes = (q->avail <= q->max_chunk ? q->avail : q->max_chunk);
  q->avail -= bytes;

  dprintf("flow_queue_fire: t=%p q=%p idx=%u gidx=%u bytes=%u avail=%u rate=%u\n", t, q, idx, idx, bytes, q->avail, q->rate);
  if (q->rate > 0) 
  {
    q->next_ts = flow_queue_new_ts(t, q, bytes);
  }

  if (q->avail > 0) 
  {
    flow_queue_activate(t, fc, q, idx);
  }

  *q_bytes = bytes;
  *q_id = idx;

#ifdef FLEXNIC_TRACE_QMAN
  struct flexnic_trace_entry_qman_event evt = {
      .id = *q_id, .bytes = bytes,
    };
  trace_event(FLEXNIC_TRACE_EV_QMEVT, sizeof(evt), &evt);
#endif
}

static inline void flow_queue_activate(struct qman_thread *t, struct flow_cont *fc,
    struct flow_queue *q, uint32_t idx)
{
  if (q->rate == 0) {
    flow_queue_activate_nolimit(fc, q, idx);
  } else {
    flow_queue_activate_skiplist(t, fc, q, idx);
  }
}

/*****************************************************************************/

/*****************************************************************************/
/* Helper functions for unit tests */

void qman_free_app_cont(struct dataplane_context *ctx)
{
  int i;
  struct app_cont *ac;
  struct app_queue *aq;
  struct flow_cont *fc;

  ac = ctx->qman.a_cont;

  for (i = 0; i < FLEXNIC_PL_APPST_NUM; i++)
  {
    aq = &ac->queues[i];
    fc = aq->f_cont;
    free(fc->queues);
    free(fc);
  }

  free(ac->queues);
  free(ac);
}

uint32_t qman_app_get_avail(struct dataplane_context *ctx, uint32_t app_id)
{
  uint32_t avail;
  struct app_cont *ac;
  struct app_queue *aq;
  
  ac = ctx->qman.a_cont;
  aq = &ac->queues[app_id];
  avail = aq->avail;
  
  return avail;
}