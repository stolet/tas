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

#ifndef BUDGET_DEBUG_H_
#define BUDGET_DEBUG_H_

#include <stdint.h>
#include <stdio.h>

#include <tas_memif.h>
#include <virtuoso.h>

#ifdef BUDGET_DEBUG_STATS

#define BUDGET_DEBUG_WINDOW_US 1000000ULL
#define BUDGET_DEBUG_PERCENT_SCALE 10
#define BUDGET_DEBUG_PERCENT_BIN_TENTHS 5
#define BUDGET_DEBUG_NONNEG_MAX_TENTHS (1000 * BUDGET_DEBUG_PERCENT_SCALE)
#define BUDGET_DEBUG_SIGNED_MIN_TENTHS (-1000 * BUDGET_DEBUG_PERCENT_SCALE)
#define BUDGET_DEBUG_SIGNED_MAX_TENTHS (100 * BUDGET_DEBUG_PERCENT_SCALE)
#define BUDGET_DEBUG_NONNEG_REGULAR_BINS \
  ((BUDGET_DEBUG_NONNEG_MAX_TENTHS / BUDGET_DEBUG_PERCENT_BIN_TENTHS) + 1)
#define BUDGET_DEBUG_NONNEG_BIN_COUNT \
  (BUDGET_DEBUG_NONNEG_REGULAR_BINS + 1)
#define BUDGET_DEBUG_SIGNED_REGULAR_BINS \
  (((BUDGET_DEBUG_SIGNED_MAX_TENTHS - BUDGET_DEBUG_SIGNED_MIN_TENTHS) / \
      BUDGET_DEBUG_PERCENT_BIN_TENTHS) + 1)
#define BUDGET_DEBUG_SIGNED_BIN_COUNT \
  (BUDGET_DEBUG_SIGNED_REGULAR_BINS + 2)

struct budget_debug_fast_snapshot {
  uint64_t consumed_total;
  uint64_t consumed_vm[FLEXNIC_PL_VMST_NUM];
  uint64_t work_conserving_vm[FLEXNIC_PL_VMST_NUM];
};

struct budget_debug_u64_stats {
  uint64_t count;
  uint64_t sum;
  uint64_t min;
  uint64_t max;
};

struct budget_debug_pct_nonneg_stats {
  uint64_t count;
  int64_t sum_tenths;
  int32_t min_tenths;
  int32_t max_tenths;
  uint32_t bins[BUDGET_DEBUG_NONNEG_BIN_COUNT];
};

struct budget_debug_pct_signed_stats {
  uint64_t count;
  int64_t sum_tenths;
  int32_t min_tenths;
  int32_t max_tenths;
  uint32_t bins[BUDGET_DEBUG_SIGNED_BIN_COUNT];
};

struct budget_debug_core_window {
  uint64_t periods;
  uint64_t zero_dist_periods;
  struct budget_debug_u64_stats consumed;
  struct budget_debug_u64_stats distributed;
  struct budget_debug_u64_stats work_conserving;
  struct budget_debug_u64_stats total_consumed;
  struct budget_debug_pct_nonneg_stats utilization;
  struct budget_debug_pct_nonneg_stats consumed_over_elapsed;
  struct budget_debug_pct_nonneg_stats distributed_over_elapsed;
  struct budget_debug_pct_nonneg_stats work_conserving_utilization;
  struct budget_debug_pct_nonneg_stats work_conserving_over_elapsed;
};

struct budget_debug_vm_window {
  struct budget_debug_u64_stats consumed;
  struct budget_debug_u64_stats distributed;
  struct budget_debug_u64_stats work_conserving;
  struct budget_debug_pct_nonneg_stats distributed_used;
  struct budget_debug_pct_nonneg_stats cap_used;
  struct budget_debug_pct_nonneg_stats work_conserving_used;
  struct budget_debug_pct_signed_stats budget_pre;
  struct budget_debug_pct_signed_stats budget_post;
};

struct budget_debug_window {
  uint64_t start_us;
  uint64_t completed_intervals;
  struct budget_debug_u64_stats elapsed_cycles;
  uint64_t prev_core_distributed[FLEXNIC_PL_APPST_CTX_MCS];
  uint64_t cur_core_distributed[FLEXNIC_PL_APPST_CTX_MCS];
  uint64_t prev_vm_distributed[FLEXNIC_PL_APPST_CTX_MCS][FLEXNIC_PL_VMST_NUM];
  uint64_t cur_vm_distributed[FLEXNIC_PL_APPST_CTX_MCS][FLEXNIC_PL_VMST_NUM];
  struct budget_debug_core_window cores[FLEXNIC_PL_APPST_CTX_MCS];
  struct budget_debug_vm_window
      vms[FLEXNIC_PL_APPST_CTX_MCS][FLEXNIC_PL_VMST_NUM];
};

void budget_debug_window_begin(struct budget_debug_window *window,
    uint64_t now_us, uint16_t num_cores);
void budget_debug_window_clear_core_distributions(
    struct budget_debug_window *window, uint16_t num_cores);
void budget_debug_record_core_interval(struct budget_debug_window *window,
    uint16_t core_id, const struct budget_debug_fast_snapshot *snapshot,
    const uint16_t *vm_ids, uint16_t vm_count, uint64_t max_budget,
    uint64_t elapsed_cycles);
void budget_debug_record_vm_distribution(struct budget_debug_window *window,
    uint16_t core_id, uint16_t vm_id, int64_t budget_before,
    int64_t budget_after, uint64_t applied_distribution,
    uint64_t max_budget);
void budget_debug_publish_core_distribution(struct budget_debug_window *window,
    uint16_t core_id);
void budget_debug_window_maybe_print(struct budget_debug_window *window,
    FILE *out, uint64_t now_us, uint16_t num_cores, const uint16_t *vm_ids,
    uint16_t vm_count);

#endif /* BUDGET_DEBUG_STATS */

#endif /* ndef BUDGET_DEBUG_H_ */
