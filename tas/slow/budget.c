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
#include <stdio.h>
#include <stdatomic.h>

#include <rte_pause.h>

#include <budget_debug.h>
#include <tas.h>
#include <utils.h>
#include <virtuoso.h>

#include "internal.h"

static void init_vm_weights(double *weights);
static void slow_budget_boost(int vmid, int64_t incr);
static void budget_update(uint64_t cur_tsc);

uint64_t get_budget_delta(int vmid, int ctxid);
void boost_budget(int vmid, int ctxid, int64_t incr);
#ifdef BUDGET_DEBUG_STATS
int64_t tas_get_budget_raw(int vmid, int ctxid);
void tas_budget_debug_snapshot_core(int ctxid,
    struct budget_debug_fast_snapshot *snapshot);
static struct budget_debug_window budget_debug_window;
#endif

static double vm_weights[FLEXNIC_PL_VMST_NUM];
static uint64_t last_bu_update_ts = 0;
static uint64_t budget_ts = 0;
static uint64_t budget_period_tsc = 0;
static int budget_threads_launched = 0;
static atomic_int budget_ready = 0;
static struct budget_statistics slow_budgets[FLEXNIC_PL_VMST_NUM];

void budget_init(int threads_launched)
{
  int vmid;

  budget_threads_launched = threads_launched;
  budget_period_tsc = config.bu_update_freq * util_timeout_tsc_per_us();
  budget_ts = 0;
  last_bu_update_ts = 0;
  init_vm_weights(vm_weights);

  for (vmid = 0; vmid < FLEXNIC_PL_VMST_NUM; vmid++) {
    slow_budgets[vmid].budget = config.bu_max_budget;
    slow_budgets[vmid].cycles_poll = 0;
    slow_budgets[vmid].cycles_tx = 0;
    slow_budgets[vmid].cycles_rx = 0;
    slow_budgets[vmid].cycles_total = 0;
  }

  atomic_store_explicit(&budget_ready, 1, memory_order_release);
}

void budget_poll(void)
{
  if (!config.bu_dedicated) {
    budget_update(util_rdtsc());
  }
}

int budget_thread(void *arg)
{
  (void) arg;

  while (atomic_load_explicit(&budget_ready, memory_order_acquire) == 0) {
    rte_pause();
  }

  while (1) {
    budget_update(util_rdtsc());
    rte_pause();
  }

  return 0;
}

static void budget_update(uint64_t cur_tsc)
{
  uint32_t vmid, ctxid;
  uint16_t vm_count;
  int64_t incr;
  int64_t total_budget;
  double weights_sum = 0;
#ifdef BUDGET_DEBUG_STATS
  uint64_t now_us;
  struct budget_debug_fast_snapshot debug_snapshot;
  int64_t budget_before;
  int64_t budget_after;
  uint64_t applied_distribution;
  uint16_t vm_ids[FLEXNIC_PL_VMST_NUM];
#endif

  if (cur_tsc - budget_ts < budget_period_tsc)
    return;
  
  budget_ts = cur_tsc;
  total_budget = config.bu_boost * (cur_tsc - last_bu_update_ts);
  vm_count = tas_reg_nvm_get();

#ifdef BUDGET_DEBUG_STATS
  now_us = util_timeout_time_us();
  for (vmid = 0; vmid < vm_count; vmid++) {
    vm_ids[vmid] = (uint16_t) vmid;
  }
#endif

  if (vm_count == 0) {
#ifdef BUDGET_DEBUG_STATS
    budget_debug_window_clear_core_distributions(&budget_debug_window,
        budget_threads_launched);
    budget_debug_window_maybe_print(&budget_debug_window, stderr, now_us,
        budget_threads_launched, vm_ids, vm_count);
#endif
    last_bu_update_ts = cur_tsc;
    return;
  }

#ifdef BUDGET_DEBUG_STATS
  budget_debug_window_begin(&budget_debug_window, now_us,
      budget_threads_launched);
  for (ctxid = 0; ctxid < budget_threads_launched; ctxid++) {
    tas_budget_debug_snapshot_core(ctxid, &debug_snapshot);
    budget_debug_record_core_interval(&budget_debug_window, ctxid,
        &debug_snapshot, vm_ids, vm_count,
        config.bu_max_budget, cur_tsc - last_bu_update_ts);
  }
#endif

  for (vmid = 0; vmid < vm_count; vmid++) {
    weights_sum += vm_weights[vmid];
  }

  for (vmid = 0; vmid < vm_count; vmid++) 
  {
    incr = ((total_budget * vm_weights[vmid]) / weights_sum);
    assert(incr >= 0);

    for (ctxid = 0; ctxid < budget_threads_launched; ctxid++) 
    {
#ifdef BUDGET_DEBUG_STATS
      budget_before = tas_get_budget_raw(vmid, ctxid);
#endif

      boost_budget(vmid, ctxid, incr);

#ifdef BUDGET_DEBUG_STATS
      budget_after = tas_get_budget_raw(vmid, ctxid);
      if (budget_after > budget_before) {
        applied_distribution = (uint64_t) (budget_after - budget_before);
      } else {
        applied_distribution = 0;
      }
      budget_debug_record_vm_distribution(&budget_debug_window, ctxid, vmid,
          budget_before, budget_after, applied_distribution,
          config.bu_max_budget);
#endif
    }

    slow_budget_boost(vmid, incr);
  }

#ifdef BUDGET_DEBUG_STATS
  for (ctxid = 0; ctxid < budget_threads_launched; ctxid++) {
    budget_debug_publish_core_distribution(&budget_debug_window, ctxid);
  }

  budget_debug_window_maybe_print(&budget_debug_window, stderr, now_us,
      budget_threads_launched, vm_ids, vm_count);
#endif

  last_bu_update_ts = cur_tsc;
}

static void init_vm_weights(double *weights)
{
  int vmid;

  for (vmid = 0; vmid < FLEXNIC_PL_VMST_NUM; vmid++) {
    weights[vmid] = 1;
  }
}

static void slow_budget_boost(int vmid, int64_t incr)
{
  int64_t new_budget;

  new_budget = slow_budgets[vmid].budget + incr;
  if (new_budget > (int64_t) config.bu_max_budget) {
    slow_budgets[vmid].budget = config.bu_max_budget;
  } else {
    slow_budgets[vmid].budget = new_budget;
  }
}

int slow_budget_available(uint16_t vmid)
{
  return slow_budgets[vmid].budget > 0;
}

void slow_budget_consume(uint16_t vmid, enum slow_budget_phase phase,
    uint64_t amount)
{
  struct budget_statistics *stats;

  if (amount == 0)
    return;

  stats = &slow_budgets[vmid];
  stats->budget -= (int64_t) amount;

  switch (phase) {
    case SLOW_BUDGET_PHASE_POLL:
      stats->cycles_poll += amount;
      break;

    case SLOW_BUDGET_PHASE_TX:
      stats->cycles_tx += amount;
      break;

    case SLOW_BUDGET_PHASE_RX:
      stats->cycles_rx += amount;
      break;
  }

  stats->cycles_total += amount;
}
