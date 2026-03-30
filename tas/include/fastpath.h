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

#ifndef FASTPATH_H_
#define FASTPATH_H_

#include <stdbool.h>
#include <stdint.h>

#include <rte_interrupts.h>

#include <tas_memif.h>
#include <virtuoso.h>
#include <utils_rng.h>

#define BATCH_SIZE 16
#define BUFCACHE_SIZE 128
#define TXBUF_SIZE (2 * BATCH_SIZE)

#define POLL_PHASE 1
#define TX_PHASE 2
#define RX_PHASE 3

struct network_thread {
  struct rte_mempool *pool;
  uint16_t queue_id;
};

/** Skiplist: #levels */
#define QMAN_SKIPLIST_LEVELS 4

struct qman_thread {
  /* modified by owner thread */
  /************************************/
  struct vm_qman *vqman;
  struct utils_rng rng;
};

struct polled_vm {
  uint32_t id;
  uint32_t next;
  uint32_t prev;
  uint16_t flags;
  uint32_t nctx;
  uint32_t poll_next_ctx;

  /* polled contexts for each app */
  uint32_t act_ctx_head;
  uint32_t act_ctx_tail;
};

struct vm_budget {
  uint16_t vmid;
  volatile int64_t budget;
  volatile uint64_t cycles_poll;
  volatile uint64_t cycles_tx;
  volatile uint64_t cycles_rx;
};

static inline int64_t vm_budget_read_relaxed(const struct vm_budget *budget)
{
  return __atomic_load_n(&budget->budget, __ATOMIC_RELAXED);
}

static inline void vm_budget_write_relaxed(struct vm_budget *budget,
    int64_t value)
{
  __atomic_store_n(&budget->budget, value, __ATOMIC_RELAXED);
}

static inline int64_t vm_budget_fetch_add_relaxed(struct vm_budget *budget,
    int64_t value)
{
  return __atomic_fetch_add(&budget->budget, value, __ATOMIC_RELAXED);
}

static inline int64_t vm_budget_fetch_sub_relaxed(struct vm_budget *budget,
    int64_t value)
{
  return __atomic_fetch_sub(&budget->budget, value, __ATOMIC_RELAXED);
}

static inline int64_t vm_budget_exchange_relaxed(struct vm_budget *budget,
    int64_t value)
{
  return __atomic_exchange_n(&budget->budget, value, __ATOMIC_RELAXED);
}

static inline uint64_t vm_budget_cycles_read_relaxed(volatile uint64_t *cycles)
{
  return __atomic_load_n(cycles, __ATOMIC_RELAXED);
}

static inline uint64_t vm_budget_cycles_exchange_relaxed(
    volatile uint64_t *cycles, uint64_t value)
{
  return __atomic_exchange_n(cycles, value, __ATOMIC_RELAXED);
}

struct dataplane_batch_stats {
  uint64_t rx_polls;
  uint64_t rx_total;
  uint64_t qm_polls;
  uint64_t qm_total;
  uint64_t qs_polls;
  uint64_t qs_total;
};

struct dataplane_context {
  struct network_thread net;
  struct qman_thread qman;
  struct rte_ring *qman_fwd_ring;
  uint16_t id;
  int evfd;
  struct rte_epoll_event ev;

  /********************************************************/
  /* arx cache */
  struct flextcp_pl_arx arx_cache[BATCH_SIZE];
  uint16_t arx_ctx[BATCH_SIZE];
  uint16_t arx_vm[BATCH_SIZE];
  uint16_t arx_num;

  /********************************************************/
  /* send buffer */
  struct network_buf_handle *tx_handles[TXBUF_SIZE];
  uint16_t tx_num;

  /********************************************************/
  /* polling queues topology */
  uint32_t poll_next_vm;
  uint32_t poll_next_kernel_vm;
  struct polled_vm polled_vms[FLEXNIC_PL_VMST_NUM];

  /********************************************************/
  /* group resource budget */
  int counters_total;
  int vm_counters[FLEXNIC_PL_VMST_NUM];
  int64_t budget_cache[FLEXNIC_PL_VMST_NUM];
  struct vm_budget budgets[FLEXNIC_PL_VMST_NUM];

  /********************************************************/
  /* pre-allocated buffers for polling doorbells and queue manager */
  struct network_buf_handle *bufcache_handles[BUFCACHE_SIZE];
  uint16_t bufcache_num;
  uint16_t bufcache_head;

  uint64_t loadmon_cyc_busy;

  uint64_t kernel_drop;
#ifdef BATCH_SIZE_STATS
  uint64_t stat_batch_rx_polls;
  uint64_t stat_batch_rx_total;
  uint64_t stat_batch_qm_polls;
  uint64_t stat_batch_qm_total;
  uint64_t stat_batch_qs_polls;
  uint64_t stat_batch_qs_total;
#endif
#ifdef BUDGET_DEBUG_STATS
  volatile uint64_t budget_debug_consumed_total;
  volatile uint64_t budget_debug_consumed_vm[FLEXNIC_PL_VMST_NUM];
  volatile uint64_t budget_debug_wc_cycles[FLEXNIC_PL_VMST_NUM];
#endif
#ifdef DATAPLANE_STATS
  /********************************************************/
  /* Stats */
  uint64_t stat_qm_poll;
  uint64_t stat_qm_empty;
  uint64_t stat_qm_total;

  uint64_t stat_rx_poll;
  uint64_t stat_rx_empty;
  uint64_t stat_rx_total;

  uint64_t stat_qs_poll;
  uint64_t stat_qs_empty;
  uint64_t stat_qs_total;

  uint64_t stat_cyc_db;
  uint64_t stat_cyc_qm;
  uint64_t stat_cyc_rx;
  uint64_t stat_cyc_qs;
#endif
};

extern struct dataplane_context **ctxs;

#ifdef BUDGET_DEBUG_STATS
static inline void budget_debug_record_consume(
    struct dataplane_context *ctx, uint16_t vmid, uint64_t amount)
{
  if (amount == 0)
    return;

  __atomic_fetch_add(&ctx->budget_debug_consumed_total, amount,
      __ATOMIC_RELAXED);
  __atomic_fetch_add(&ctx->budget_debug_consumed_vm[vmid], amount,
      __ATOMIC_RELAXED);
}

static inline void budget_debug_record_wc(
    struct dataplane_context *ctx, uint16_t vmid, uint64_t amount)
{
  if (amount == 0)
    return;

  __atomic_fetch_add(&ctx->budget_debug_wc_cycles[vmid], amount,
      __ATOMIC_RELAXED);
}
#endif

static inline int64_t budget_cache_read_relaxed(
    const struct dataplane_context *ctx, uint16_t vmid)
{
  return __atomic_load_n(&ctx->budget_cache[vmid], __ATOMIC_RELAXED);
}

static inline void budget_cache_write_relaxed(
    struct dataplane_context *ctx, uint16_t vmid, int64_t value)
{
  __atomic_store_n(&ctx->budget_cache[vmid], value, __ATOMIC_RELAXED);
}

static inline int64_t budget_effective_read_relaxed(
    const struct dataplane_context *ctx, uint16_t vmid)
{
  return budget_cache_read_relaxed(ctx, vmid) +
      vm_budget_read_relaxed(&ctx->budgets[vmid]);
}

static inline int64_t budget_cache_refill(
    struct dataplane_context *ctx, uint16_t vmid)
{
  int64_t cache;

  cache = budget_cache_read_relaxed(ctx, vmid);
  if (cache > 0)
    return cache;

  cache += vm_budget_exchange_relaxed(&ctx->budgets[vmid], 0);
  budget_cache_write_relaxed(ctx, vmid, cache);
  return cache;
}

static inline int budget_available(struct dataplane_context *ctx,
    uint16_t vmid)
{
  if (budget_cache_read_relaxed(ctx, vmid) > 0)
    return 1;

  return budget_cache_refill(ctx, vmid) > 0;
}

static inline void budget_consume(struct dataplane_context *ctx,
    uint16_t vmid, uint64_t amount)
{
  int64_t cache;

  if (amount == 0)
    return;

  cache = budget_cache_read_relaxed(ctx, vmid);
  if (cache <= 0 || (uint64_t) cache < amount)
    cache = budget_cache_refill(ctx, vmid);

  cache -= (int64_t) amount;
  budget_cache_write_relaxed(ctx, vmid, cache);
#ifdef BUDGET_DEBUG_STATS
  budget_debug_record_consume(ctx, vmid, amount);
#endif
}

int dataplane_init(void);
int dataplane_context_init(struct dataplane_context *ctx);
void dataplane_context_destroy(struct dataplane_context *ctx);
void dataplane_loop(struct dataplane_context *ctx);
#ifdef BATCH_SIZE_STATS
void dataplane_batch_stats_collect(struct dataplane_batch_stats *stats);
#else
static inline void dataplane_batch_stats_collect(
    struct dataplane_batch_stats *stats)
{
  (void) stats;
}
#endif
#ifdef DATAPLANE_STATS
void dataplane_dump_stats(void);
#endif

#endif /* ndef FASTPATH_H_ */
