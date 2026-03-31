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

#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

#include <budget_debug.h>

#ifdef BUDGET_DEBUG_STATS

static void budget_debug_reset_summaries(struct budget_debug_window *window,
    uint64_t now_us);
static void budget_debug_record_interval_common(
    struct budget_debug_core_window *core_window,
    struct budget_debug_vm_window *vm_windows, uint64_t prev_distributed,
    const uint64_t *prev_vm_distributed,
    const struct budget_debug_fast_snapshot *snapshot, const uint16_t *vm_ids,
    uint16_t vm_count, uint64_t max_budget, uint64_t elapsed_cycles,
    struct budget_debug_u64_stats *elapsed_cycles_stats);
static void budget_debug_record_distribution_common(
    struct budget_debug_vm_window *vm_windows, uint64_t *cur_distributed,
    uint16_t vm_id, int64_t budget_before, int64_t budget_after,
    uint64_t applied_distribution, uint64_t max_budget);
static void budget_debug_publish_distribution_common(uint64_t *prev_distributed,
    uint64_t *cur_distributed);
static void budget_debug_print_section(FILE *out, const char *label,
    const struct budget_debug_core_window *core_window,
    const struct budget_debug_vm_window *vm_windows, const uint16_t *vm_ids,
    uint16_t vm_count);
static void budget_debug_u64_add(struct budget_debug_u64_stats *stats,
    uint64_t value);
static void budget_debug_pct_nonneg_add(
    struct budget_debug_pct_nonneg_stats *stats, int32_t tenths);
static void budget_debug_pct_signed_add(
    struct budget_debug_pct_signed_stats *stats, int32_t tenths);
static uint64_t budget_debug_u64_avg(const struct budget_debug_u64_stats *stats);
static int32_t budget_debug_pct_avg_nonneg(
    const struct budget_debug_pct_nonneg_stats *stats);
static int32_t budget_debug_pct_avg_signed(
    const struct budget_debug_pct_signed_stats *stats);
static int32_t budget_debug_pct_quantile_nonneg(
    const struct budget_debug_pct_nonneg_stats *stats, uint32_t percentile);
static int32_t budget_debug_pct_quantile_signed(
    const struct budget_debug_pct_signed_stats *stats, uint32_t percentile);
static int32_t budget_debug_ratio_tenths_u64(uint64_t numer, uint64_t denom);
static int32_t budget_debug_ratio_tenths_i64(int64_t numer, uint64_t denom);
static void budget_debug_format_tenths(char *buf, size_t len, int32_t tenths);
static void budget_debug_format_u64_stats(char *buf, size_t len,
    const struct budget_debug_u64_stats *stats);
static void budget_debug_format_pct_nonneg(char *buf, size_t len,
    const struct budget_debug_pct_nonneg_stats *stats);
static void budget_debug_format_pct_signed(char *buf, size_t len,
    const struct budget_debug_pct_signed_stats *stats);

void budget_debug_window_begin(struct budget_debug_window *window,
    uint64_t now_us, uint16_t num_cores)
{
  uint16_t i;

  if (window->start_us == 0) {
    budget_debug_reset_summaries(window, now_us);
  }

  window->completed_intervals++;
  for (i = 0; i < num_cores; i++) {
    window->cur_core_distributed[i] = 0;
  }
}

void budget_debug_window_clear_core_distributions(
    struct budget_debug_window *window, uint16_t num_cores)
{
  uint16_t i;
  uint16_t vmid;

  for (i = 0; i < num_cores; i++) {
    window->prev_core_distributed[i] = 0;
    window->cur_core_distributed[i] = 0;
    for (vmid = 0; vmid < FLEXNIC_PL_VMST_NUM; vmid++) {
      window->prev_vm_distributed[i][vmid] = 0;
      window->cur_vm_distributed[i][vmid] = 0;
    }
  }

  window->prev_slow_distributed = 0;
  window->cur_slow_distributed = 0;
  for (vmid = 0; vmid < FLEXNIC_PL_VMST_NUM; vmid++) {
    window->prev_slow_vm_distributed[vmid] = 0;
    window->cur_slow_vm_distributed[vmid] = 0;
  }
}

void budget_debug_record_core_interval(struct budget_debug_window *window,
    uint16_t core_id, const struct budget_debug_fast_snapshot *snapshot,
    const uint16_t *vm_ids, uint16_t vm_count, uint64_t max_budget,
    uint64_t elapsed_cycles)
{
  budget_debug_record_interval_common(&window->cores[core_id],
      window->vms[core_id], window->prev_core_distributed[core_id],
      window->prev_vm_distributed[core_id], snapshot, vm_ids, vm_count,
      max_budget, elapsed_cycles,
      (core_id == 0 ? &window->elapsed_cycles : NULL));
}

void budget_debug_record_slow_interval(struct budget_debug_window *window,
    const struct budget_debug_fast_snapshot *snapshot, const uint16_t *vm_ids,
    uint16_t vm_count, uint64_t max_budget, uint64_t elapsed_cycles)
{
  budget_debug_record_interval_common(&window->slow, window->slow_vms,
      window->prev_slow_distributed, window->prev_slow_vm_distributed,
      snapshot, vm_ids, vm_count, max_budget, elapsed_cycles, NULL);
}

void budget_debug_record_vm_distribution(struct budget_debug_window *window,
    uint16_t core_id, uint16_t vm_id, int64_t budget_before,
    int64_t budget_after, uint64_t applied_distribution,
    uint64_t max_budget)
{
  budget_debug_record_distribution_common(window->vms[core_id],
      &window->cur_core_distributed[core_id], vm_id, budget_before,
      budget_after, applied_distribution, max_budget);
  window->cur_vm_distributed[core_id][vm_id] += applied_distribution;
}

void budget_debug_record_slow_distribution(struct budget_debug_window *window,
    uint16_t vm_id, int64_t budget_before, int64_t budget_after,
    uint64_t applied_distribution, uint64_t max_budget)
{
  budget_debug_record_distribution_common(window->slow_vms,
      &window->cur_slow_distributed, vm_id, budget_before, budget_after,
      applied_distribution, max_budget);
  window->cur_slow_vm_distributed[vm_id] += applied_distribution;
}

void budget_debug_publish_core_distribution(struct budget_debug_window *window,
    uint16_t core_id)
{
  uint16_t vmid;

  window->prev_core_distributed[core_id] = window->cur_core_distributed[core_id];
  window->cur_core_distributed[core_id] = 0;
  for (vmid = 0; vmid < FLEXNIC_PL_VMST_NUM; vmid++) {
    window->prev_vm_distributed[core_id][vmid] =
        window->cur_vm_distributed[core_id][vmid];
    window->cur_vm_distributed[core_id][vmid] = 0;
  }
}

void budget_debug_publish_slow_distribution(struct budget_debug_window *window)
{
  uint16_t vmid;

  budget_debug_publish_distribution_common(&window->prev_slow_distributed,
      &window->cur_slow_distributed);
  for (vmid = 0; vmid < FLEXNIC_PL_VMST_NUM; vmid++) {
    window->prev_slow_vm_distributed[vmid] =
        window->cur_slow_vm_distributed[vmid];
    window->cur_slow_vm_distributed[vmid] = 0;
  }
}

void budget_debug_window_maybe_print(struct budget_debug_window *window,
    FILE *out, uint64_t now_us, uint16_t num_cores, const uint16_t *vm_ids,
    uint16_t vm_count)
{
  uint16_t core_id;
  uint64_t elapsed_us;
  char label[16];
  char elapsed[80];

  if (window->start_us == 0 || window->completed_intervals == 0) {
    return;
  }

  elapsed_us = now_us - window->start_us;
  if (elapsed_us < BUDGET_DEBUG_WINDOW_US) {
    return;
  }

  fprintf(out, "\n");
  fprintf(out, "================ Budget Debug (%" PRIu64
      " us, intervals=%" PRIu64 ") ================\n",
      elapsed_us, window->completed_intervals);
  budget_debug_format_u64_stats(elapsed, sizeof(elapsed),
      &window->elapsed_cycles);
  fprintf(out, "  %-*s avg/min/max              : %s\n\n",
      17, "elapsed cycles", elapsed);

  for (core_id = 0; core_id < num_cores; core_id++) {
    snprintf(label, sizeof(label), "[core %02u]", core_id);
    budget_debug_print_section(out, label, &window->cores[core_id],
        window->vms[core_id], vm_ids, vm_count);
  }

  budget_debug_print_section(out, "[slow]", &window->slow, window->slow_vms,
      vm_ids, vm_count);

  fprintf(out,
      "================================================================\n");
  fflush(out);

  budget_debug_reset_summaries(window, now_us);
}

static void budget_debug_reset_summaries(struct budget_debug_window *window,
    uint64_t now_us)
{
  memset(&window->elapsed_cycles, 0, sizeof(window->elapsed_cycles));
  memset(window->cores, 0, sizeof(window->cores));
  memset(&window->slow, 0, sizeof(window->slow));
  memset(window->vms, 0, sizeof(window->vms));
  memset(window->slow_vms, 0, sizeof(window->slow_vms));
  memset(window->cur_vm_distributed, 0, sizeof(window->cur_vm_distributed));
  memset(window->cur_core_distributed, 0, sizeof(window->cur_core_distributed));
  memset(window->cur_slow_vm_distributed, 0,
      sizeof(window->cur_slow_vm_distributed));
  window->cur_slow_distributed = 0;
  window->start_us = now_us;
  window->completed_intervals = 0;
}

static void budget_debug_record_interval_common(
    struct budget_debug_core_window *core_window,
    struct budget_debug_vm_window *vm_windows, uint64_t prev_distributed,
    const uint64_t *prev_vm_distributed,
    const struct budget_debug_fast_snapshot *snapshot, const uint16_t *vm_ids,
    uint16_t vm_count, uint64_t max_budget, uint64_t elapsed_cycles,
    struct budget_debug_u64_stats *elapsed_cycles_stats)
{
  uint16_t vm_idx;
  uint16_t vm_id;
  uint64_t consumed_cycles = snapshot->consumed_total;
  uint64_t total_work_conserving_cycles = 0;

  if (elapsed_cycles_stats != NULL) {
    budget_debug_u64_add(elapsed_cycles_stats, elapsed_cycles);
  }

  core_window->periods++;
  budget_debug_u64_add(&core_window->consumed, consumed_cycles);
  budget_debug_u64_add(&core_window->distributed, prev_distributed);

  for (vm_idx = 0; vm_idx < vm_count; vm_idx++) {
    struct budget_debug_vm_window *vm_window;
    uint64_t work_conserving_cycles;

    vm_id = vm_ids[vm_idx];
    vm_window = &vm_windows[vm_id];
    work_conserving_cycles = snapshot->work_conserving_vm[vm_id];
    total_work_conserving_cycles += work_conserving_cycles;

    budget_debug_u64_add(&vm_window->consumed, snapshot->consumed_vm[vm_id]);
    budget_debug_u64_add(&vm_window->work_conserving, work_conserving_cycles);
    budget_debug_pct_nonneg_add(&vm_window->cap_used,
        budget_debug_ratio_tenths_u64(snapshot->consumed_vm[vm_id],
            max_budget));

    if (prev_vm_distributed[vm_id] != 0) {
      budget_debug_pct_nonneg_add(&vm_window->distributed_used,
          budget_debug_ratio_tenths_u64(snapshot->consumed_vm[vm_id],
              prev_vm_distributed[vm_id]));
      budget_debug_pct_nonneg_add(&vm_window->work_conserving_used,
          budget_debug_ratio_tenths_u64(work_conserving_cycles,
              prev_vm_distributed[vm_id]));
    }
  }

  budget_debug_u64_add(&core_window->work_conserving,
      total_work_conserving_cycles);
  budget_debug_u64_add(&core_window->total_consumed,
      consumed_cycles + total_work_conserving_cycles);

  if (elapsed_cycles != 0) {
    budget_debug_pct_nonneg_add(&core_window->consumed_over_elapsed,
        budget_debug_ratio_tenths_u64(consumed_cycles, elapsed_cycles));
    budget_debug_pct_nonneg_add(&core_window->distributed_over_elapsed,
        budget_debug_ratio_tenths_u64(prev_distributed, elapsed_cycles));
    budget_debug_pct_nonneg_add(&core_window->work_conserving_over_elapsed,
        budget_debug_ratio_tenths_u64(total_work_conserving_cycles,
            elapsed_cycles));
  }

  if (prev_distributed == 0) {
    core_window->zero_dist_periods++;
  } else {
    budget_debug_pct_nonneg_add(&core_window->utilization,
        budget_debug_ratio_tenths_u64(consumed_cycles, prev_distributed));
    budget_debug_pct_nonneg_add(&core_window->work_conserving_utilization,
        budget_debug_ratio_tenths_u64(total_work_conserving_cycles,
            prev_distributed));
  }
}

static void budget_debug_record_distribution_common(
    struct budget_debug_vm_window *vm_windows, uint64_t *cur_distributed,
    uint16_t vm_id, int64_t budget_before, int64_t budget_after,
    uint64_t applied_distribution, uint64_t max_budget)
{
  struct budget_debug_vm_window *vm_window = &vm_windows[vm_id];

  budget_debug_u64_add(&vm_window->distributed, applied_distribution);
  budget_debug_pct_signed_add(&vm_window->budget_pre,
      budget_debug_ratio_tenths_i64(budget_before, max_budget));
  budget_debug_pct_signed_add(&vm_window->budget_post,
      budget_debug_ratio_tenths_i64(budget_after, max_budget));
  *cur_distributed += applied_distribution;
}

static void budget_debug_publish_distribution_common(uint64_t *prev_distributed,
    uint64_t *cur_distributed)
{
  *prev_distributed = *cur_distributed;
  *cur_distributed = 0;
}

static void budget_debug_print_section(FILE *out, const char *label,
    const struct budget_debug_core_window *core_window,
    const struct budget_debug_vm_window *vm_windows, const uint16_t *vm_ids,
    uint16_t vm_count)
{
  const int summary_value_label_width = 17;
  const int summary_pct_label_width = 28;
  const int vm_value_label_width = 17;
  const int vm_pct_label_width = 30;
  uint16_t vm_idx;
  char consumed[80];
  char distributed[80];
  char total_consumed[80];
  char util[96];
  char consumed_elapsed[96];
  char distributed_elapsed[96];
  char core_work_conserving[80];
  char work_conserving_util[96];
  char work_conserving_elapsed[96];
  char work_conserving[80];
  char distributed_used[96];
  char cap_used[96];
  char work_conserving_used[96];
  char budget_pre[96];
  char budget_post[96];

  if (core_window->periods == 0) {
    return;
  }

  budget_debug_format_u64_stats(consumed, sizeof(consumed),
      &core_window->consumed);
  budget_debug_format_u64_stats(distributed, sizeof(distributed),
      &core_window->distributed);
  budget_debug_format_u64_stats(core_work_conserving,
      sizeof(core_work_conserving), &core_window->work_conserving);
  budget_debug_format_u64_stats(total_consumed, sizeof(total_consumed),
      &core_window->total_consumed);
  budget_debug_format_pct_nonneg(util, sizeof(util),
      &core_window->utilization);
  budget_debug_format_pct_nonneg(consumed_elapsed, sizeof(consumed_elapsed),
      &core_window->consumed_over_elapsed);
  budget_debug_format_pct_nonneg(distributed_elapsed,
      sizeof(distributed_elapsed), &core_window->distributed_over_elapsed);
  budget_debug_format_pct_nonneg(work_conserving_util,
      sizeof(work_conserving_util), &core_window->work_conserving_utilization);
  budget_debug_format_pct_nonneg(work_conserving_elapsed,
      sizeof(work_conserving_elapsed), &core_window->work_conserving_over_elapsed);

  fprintf(out, "%s periods=%" PRIu64 " zero_dist=%" PRIu64 "\n",
      label, core_window->periods, core_window->zero_dist_periods);
  fprintf(out, "  %-*s avg/min/max              : %s\n",
      summary_value_label_width, "consumed", consumed);
  fprintf(out, "  %-*s avg/min/max              : %s\n",
      summary_value_label_width, "distributed", distributed);
  fprintf(out, "  %-*s avg/min/max              : %s\n",
      summary_value_label_width, "work-cons", core_work_conserving);
  fprintf(out, "  %-*s avg/min/max              : %s\n",
      summary_value_label_width, "total consumed", total_consumed);
  fprintf(out, "  %-*s %s\n",
      summary_pct_label_width, "consumed/elapsed %", "");
  fprintf(out, "    avg/min/p25/p50/p75/max : %s\n",
      consumed_elapsed);
  fprintf(out, "  %-*s %s\n",
      summary_pct_label_width, "distributed/elapsed %", "");
  fprintf(out, "    avg/min/p25/p50/p75/max : %s\n",
      distributed_elapsed);
  fprintf(out, "  %-*s %s\n",
      summary_pct_label_width, "work-cons/elapsed %", "");
  fprintf(out, "    avg/min/p25/p50/p75/max : %s\n",
      work_conserving_elapsed);
  fprintf(out, "  %-*s %s\n",
      summary_pct_label_width, "consumed/distributed %", "");
  fprintf(out, "    avg/min/p25/p50/p75/max : %s\n",
      util);
  fprintf(out, "  %-*s %s\n",
      summary_pct_label_width, "work-cons/distributed %", "");
  fprintf(out, "    avg/min/p25/p50/p75/max : %s\n",
      work_conserving_util);

  if (vm_count == 0) {
    fprintf(out, "\n");
    return;
  }

  fprintf(out, "\n");
  for (vm_idx = 0; vm_idx < vm_count; vm_idx++) {
    uint16_t vm_id = vm_ids[vm_idx];
    const struct budget_debug_vm_window *vm_window = &vm_windows[vm_id];

    budget_debug_format_u64_stats(consumed, sizeof(consumed),
        &vm_window->consumed);
    budget_debug_format_u64_stats(distributed, sizeof(distributed),
        &vm_window->distributed);
    budget_debug_format_u64_stats(work_conserving, sizeof(work_conserving),
        &vm_window->work_conserving);
    budget_debug_format_pct_nonneg(distributed_used, sizeof(distributed_used),
        &vm_window->distributed_used);
    budget_debug_format_pct_nonneg(cap_used, sizeof(cap_used),
        &vm_window->cap_used);
    budget_debug_format_pct_nonneg(work_conserving_used,
        sizeof(work_conserving_used), &vm_window->work_conserving_used);
    budget_debug_format_pct_signed(budget_pre, sizeof(budget_pre),
        &vm_window->budget_pre);
    budget_debug_format_pct_signed(budget_post, sizeof(budget_post),
        &vm_window->budget_post);

    fprintf(out, "  vm %02u\n", vm_id);
    fprintf(out, "    %-*s avg/min/max              : %s\n",
        vm_value_label_width, "consumed", consumed);
    fprintf(out, "    %-*s avg/min/max              : %s\n",
        vm_value_label_width, "distributed", distributed);
    fprintf(out, "    %-*s avg/min/max              : %s\n",
        vm_value_label_width, "work-cons", work_conserving);
    fprintf(out, "    %-*s %s\n",
        vm_pct_label_width, "consumed/distributed %", "");
    fprintf(out, "      avg/min/p25/p50/p75/max : %s\n",
        distributed_used);
    fprintf(out, "    %-*s %s\n",
        vm_pct_label_width, "consumed/max_budget %", "");
    fprintf(out, "      avg/min/p25/p50/p75/max : %s\n",
        cap_used);
    fprintf(out, "    %-*s %s\n",
        vm_pct_label_width, "work-cons/distributed %", "");
    fprintf(out, "      avg/min/p25/p50/p75/max : %s\n",
        work_conserving_used);
    fprintf(out, "    %-*s %s\n",
        vm_pct_label_width, "budget_pre/max_budget %", "");
    fprintf(out, "      avg/min/p25/p50/p75/max : %s\n",
        budget_pre);
    fprintf(out, "    %-*s %s\n",
        vm_pct_label_width, "budget_post/max_budget %", "");
    fprintf(out, "      avg/min/p25/p50/p75/max : %s\n",
        budget_post);
    fprintf(out, "\n");
  }
}

static void budget_debug_u64_add(struct budget_debug_u64_stats *stats,
    uint64_t value)
{
  if (stats->count == 0) {
    stats->min = value;
    stats->max = value;
  } else {
    if (value < stats->min) {
      stats->min = value;
    }
    if (value > stats->max) {
      stats->max = value;
    }
  }

  stats->count++;
  stats->sum += value;
}

static void budget_debug_pct_nonneg_add(
    struct budget_debug_pct_nonneg_stats *stats, int32_t tenths)
{
  int32_t bucket;

  if (stats->count == 0) {
    stats->min_tenths = tenths;
    stats->max_tenths = tenths;
  } else {
    if (tenths < stats->min_tenths) {
      stats->min_tenths = tenths;
    }
    if (tenths > stats->max_tenths) {
      stats->max_tenths = tenths;
    }
  }

  stats->count++;
  stats->sum_tenths += tenths;

  if (tenths < 0) {
    bucket = 0;
  } else if (tenths > BUDGET_DEBUG_NONNEG_MAX_TENTHS) {
    bucket = BUDGET_DEBUG_NONNEG_REGULAR_BINS;
  } else {
    bucket = tenths / BUDGET_DEBUG_PERCENT_BIN_TENTHS;
  }

  stats->bins[bucket]++;
}

static void budget_debug_pct_signed_add(
    struct budget_debug_pct_signed_stats *stats, int32_t tenths)
{
  int32_t bucket;

  if (stats->count == 0) {
    stats->min_tenths = tenths;
    stats->max_tenths = tenths;
  } else {
    if (tenths < stats->min_tenths) {
      stats->min_tenths = tenths;
    }
    if (tenths > stats->max_tenths) {
      stats->max_tenths = tenths;
    }
  }

  stats->count++;
  stats->sum_tenths += tenths;

  if (tenths < BUDGET_DEBUG_SIGNED_MIN_TENTHS) {
    bucket = 0;
  } else if (tenths > BUDGET_DEBUG_SIGNED_MAX_TENTHS) {
    bucket = BUDGET_DEBUG_SIGNED_BIN_COUNT - 1;
  } else {
    bucket = 1 + ((tenths - BUDGET_DEBUG_SIGNED_MIN_TENTHS) /
        BUDGET_DEBUG_PERCENT_BIN_TENTHS);
  }

  stats->bins[bucket]++;
}

static uint64_t budget_debug_u64_avg(const struct budget_debug_u64_stats *stats)
{
  return stats->count == 0 ? 0 : stats->sum / stats->count;
}

static int32_t budget_debug_pct_avg_nonneg(
    const struct budget_debug_pct_nonneg_stats *stats)
{
  return stats->count == 0 ? 0 : stats->sum_tenths / (int64_t) stats->count;
}

static int32_t budget_debug_pct_avg_signed(
    const struct budget_debug_pct_signed_stats *stats)
{
  return stats->count == 0 ? 0 : stats->sum_tenths / (int64_t) stats->count;
}

static int32_t budget_debug_pct_quantile_nonneg(
    const struct budget_debug_pct_nonneg_stats *stats, uint32_t percentile)
{
  uint64_t target;
  uint64_t seen = 0;
  int i;

  if (stats->count == 0) {
    return 0;
  }

  target = (stats->count * percentile + 99) / 100;
  if (target == 0) {
    target = 1;
  }

  for (i = 0; i < BUDGET_DEBUG_NONNEG_BIN_COUNT; i++) {
    seen += stats->bins[i];
    if (seen >= target) {
      if (i >= BUDGET_DEBUG_NONNEG_REGULAR_BINS) {
        return BUDGET_DEBUG_NONNEG_MAX_TENTHS;
      }

      return i * BUDGET_DEBUG_PERCENT_BIN_TENTHS;
    }
  }

  return BUDGET_DEBUG_NONNEG_MAX_TENTHS;
}

static int32_t budget_debug_pct_quantile_signed(
    const struct budget_debug_pct_signed_stats *stats, uint32_t percentile)
{
  uint64_t target;
  uint64_t seen = 0;
  int i;

  if (stats->count == 0) {
    return 0;
  }

  target = (stats->count * percentile + 99) / 100;
  if (target == 0) {
    target = 1;
  }

  for (i = 0; i < BUDGET_DEBUG_SIGNED_BIN_COUNT; i++) {
    seen += stats->bins[i];
    if (seen >= target) {
      if (i == 0) {
        return BUDGET_DEBUG_SIGNED_MIN_TENTHS;
      }
      if (i == BUDGET_DEBUG_SIGNED_BIN_COUNT - 1) {
        return BUDGET_DEBUG_SIGNED_MAX_TENTHS;
      }

      return BUDGET_DEBUG_SIGNED_MIN_TENTHS +
          ((i - 1) * BUDGET_DEBUG_PERCENT_BIN_TENTHS);
    }
  }

  return BUDGET_DEBUG_SIGNED_MAX_TENTHS;
}

static int32_t budget_debug_ratio_tenths_u64(uint64_t numer, uint64_t denom)
{
  __int128 scaled;

  if (denom == 0) {
    return 0;
  }

  scaled = (__int128) numer * 1000;
  return (int32_t) (scaled / denom);
}

static int32_t budget_debug_ratio_tenths_i64(int64_t numer, uint64_t denom)
{
  __int128 scaled;

  if (denom == 0) {
    return 0;
  }

  scaled = (__int128) numer * 1000;
  return (int32_t) (scaled / (__int128) denom);
}

static void budget_debug_format_tenths(char *buf, size_t len, int32_t tenths)
{
  uint32_t abs_tenths;

  if (tenths < 0) {
    abs_tenths = (uint32_t) (-tenths);
    snprintf(buf, len, "-%u.%u", abs_tenths / 10, abs_tenths % 10);
  } else {
    abs_tenths = (uint32_t) tenths;
    snprintf(buf, len, "%u.%u", abs_tenths / 10, abs_tenths % 10);
  }
}

static void budget_debug_format_u64_stats(char *buf, size_t len,
    const struct budget_debug_u64_stats *stats)
{
  if (stats->count == 0) {
    snprintf(buf, len, "-");
    return;
  }

  snprintf(buf, len, "%" PRIu64 "/%" PRIu64 "/%" PRIu64,
      budget_debug_u64_avg(stats), stats->min, stats->max);
}

static void budget_debug_format_pct_nonneg(char *buf, size_t len,
    const struct budget_debug_pct_nonneg_stats *stats)
{
  char avg[16];
  char min[16];
  char p25[16];
  char p50[16];
  char p75[16];
  char max[16];

  if (stats->count == 0) {
    snprintf(buf, len, "-");
    return;
  }

  budget_debug_format_tenths(avg, sizeof(avg),
      budget_debug_pct_avg_nonneg(stats));
  budget_debug_format_tenths(min, sizeof(min), stats->min_tenths);
  budget_debug_format_tenths(p25, sizeof(p25),
      budget_debug_pct_quantile_nonneg(stats, 25));
  budget_debug_format_tenths(p50, sizeof(p50),
      budget_debug_pct_quantile_nonneg(stats, 50));
  budget_debug_format_tenths(p75, sizeof(p75),
      budget_debug_pct_quantile_nonneg(stats, 75));
  budget_debug_format_tenths(max, sizeof(max), stats->max_tenths);

  snprintf(buf, len, "%s/%s/%s/%s/%s/%s",
      avg, min, p25, p50, p75, max);
}

static void budget_debug_format_pct_signed(char *buf, size_t len,
    const struct budget_debug_pct_signed_stats *stats)
{
  char avg[16];
  char min[16];
  char p25[16];
  char p50[16];
  char p75[16];
  char max[16];

  if (stats->count == 0) {
    snprintf(buf, len, "-");
    return;
  }

  budget_debug_format_tenths(avg, sizeof(avg),
      budget_debug_pct_avg_signed(stats));
  budget_debug_format_tenths(min, sizeof(min), stats->min_tenths);
  budget_debug_format_tenths(p25, sizeof(p25),
      budget_debug_pct_quantile_signed(stats, 25));
  budget_debug_format_tenths(p50, sizeof(p50),
      budget_debug_pct_quantile_signed(stats, 50));
  budget_debug_format_tenths(p75, sizeof(p75),
      budget_debug_pct_quantile_signed(stats, 75));
  budget_debug_format_tenths(max, sizeof(max), stats->max_tenths);

  snprintf(buf, len, "%s/%s/%s/%s/%s/%s",
      avg, min, p25, p50, p75, max);
}

#endif /* BUDGET_DEBUG_STATS */
