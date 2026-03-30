#define _GNU_SOURCE
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
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <rte_launch.h>
#include <rte_pause.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

#include <utils.h>
#include <tas.h>
#include <fastpath.h>
#include <virtuoso.h>
#include "internal.h"

static void slowpath_block(uint32_t cur_ts);
static void timeout_trigger(struct timeout *to, uint8_t type, void *opaque);
static void signal_tas_ready(void);
static void slowpath_print_stats(void);
static int budget_thread(void *arg);
static void budget_thread_advance_deadline(uint64_t *deadline,
    uint64_t period_tsc, uint64_t now);
void flexnic_loadmon(uint32_t cur_ts);

#ifdef BATCH_SIZE_STATS
static void batch_stats_format_avg(char *buf, size_t len, uint64_t total,
    uint64_t polls);
#endif

struct timeout_manager timeout_mgr;
static int exited = 0;
struct kernel_statistics kstats;
uint32_t cur_ts;
int kernel_notifyfd = 0;
static int epfd;

#ifdef BATCH_SIZE_STATS
static void batch_stats_format_avg(char *buf, size_t len, uint64_t total,
    uint64_t polls)
{
  uint64_t avg_scaled = 0;

  if (polls != 0) {
    avg_scaled = (total * 100) / polls;
  }

  snprintf(buf, len, "%" PRIu64 ".%02" PRIu64, avg_scaled / 100,
      avg_scaled % 100);
}
#endif

int slowpath_main(int threads_launched, unsigned budget_lcore)
{
  struct notify_blockstate nbs;
  uint32_t loadmon_ts = 0;

  kernel_notifyfd = eventfd(0, EFD_NONBLOCK);
  assert(kernel_notifyfd != -1);

  struct epoll_event ev = {
      .events = EPOLLIN,
      .data.fd = kernel_notifyfd,
  };

  epfd = epoll_create1(0);
  assert(epfd != -1);

  int r = epoll_ctl(epfd, EPOLL_CTL_ADD, kernel_notifyfd, &ev);
  assert(r == 0);

  /* initialize timers for timeouts */
  if (util_timeout_init(&timeout_mgr, timeout_trigger, NULL))
  {
    fprintf(stderr, "timeout_init failed\n");
    return EXIT_FAILURE;
  }
  budget_init(threads_launched);

  /* initialize kni */
  if (kni_init())
  {
    fprintf(stderr, "kni_init failed\n");
    return EXIT_FAILURE;
  }

  /* initialize routing subsystem */
  if (routing_init())
  {
    fprintf(stderr, "routing_init failed\n");
    return EXIT_FAILURE;
  }

  /* connect to NIC */
  if (nicif_init())
  {
    fprintf(stderr, "nicif_init failed\n");
    return EXIT_FAILURE;
  }

  /* initialize CC */
  if (cc_init())
  {
    fprintf(stderr, "cc_init failed\n");
    return EXIT_FAILURE;
  }

  /* prepare application interface */
  if (appif_init())
  {
    fprintf(stderr, "appif_init failed\n");
    return EXIT_FAILURE;
  }

  if (arp_init())
  {
    fprintf(stderr, "arp_init failed\n");
    return EXIT_FAILURE;
  }

  if (tcp_init())
  {
    fprintf(stderr, "tcp_init failed\n");
    return EXIT_FAILURE;
  }

  r = rte_eal_remote_launch(budget_thread, NULL, budget_lcore);
  if (r != 0)
  {
    fprintf(stderr, "slowpath_main: launching budget thread failed on lcore "
        "%u (%d)\n", budget_lcore, r);
    return EXIT_FAILURE;
  }

  signal_tas_ready();

  notify_canblock_reset(&nbs);
  while (exited == 0)
  {
    unsigned n = 0;
    
    cur_ts = util_timeout_time_us();
    n += nicif_poll();
    #if VIRTUOSO_GRE
      n += ovs_poll();
    #endif
    n += cc_poll(cur_ts);
    n += appif_poll();
    n += kni_poll();
    tcp_poll();
    util_timeout_poll_ts(&timeout_mgr, cur_ts);

    if (config.fp_autoscale && cur_ts - loadmon_ts >= 10000)
    {
      flexnic_loadmon(cur_ts);
      loadmon_ts = cur_ts;
    }

    if (notify_canblock(&nbs, n != 0, util_rdtsc()))
    {
      slowpath_block(cur_ts);
      notify_canblock_reset(&nbs);
    }
  }

  return EXIT_SUCCESS;
}

static void slowpath_print_stats(void)
{
  uint64_t drops, kernel_rexmit, ecn_marked, acks;

  if (config.quiet)
    return;

  drops = __atomic_load_n(&kstats.drops, __ATOMIC_RELAXED);
  kernel_rexmit = __atomic_load_n(&kstats.kernel_rexmit, __ATOMIC_RELAXED);
  ecn_marked = __atomic_load_n(&kstats.ecn_marked, __ATOMIC_RELAXED);
  acks = __atomic_load_n(&kstats.acks, __ATOMIC_RELAXED);

#ifdef BATCH_SIZE_STATS
  {
    struct dataplane_batch_stats batch_stats;
    char rx_avg[32], qm_avg[32], qs_avg[32];

    dataplane_batch_stats_collect(&batch_stats);
    batch_stats_format_avg(rx_avg, sizeof(rx_avg), batch_stats.rx_total,
        batch_stats.rx_polls);
    batch_stats_format_avg(qm_avg, sizeof(qm_avg), batch_stats.qm_total,
        batch_stats.qm_polls);
    batch_stats_format_avg(qs_avg, sizeof(qs_avg), batch_stats.qs_total,
        batch_stats.qs_polls);
    printf("stats: drops=%" PRIu64 " k_rexmit=%" PRIu64 " ecn=%" PRIu64
           " acks=%" PRIu64 " rx_batch_avg=%s qman_batch_avg=%s"
           " queues_batch_avg=%s\n",
           drops, kernel_rexmit, ecn_marked, acks, rx_avg, qm_avg, qs_avg);
  }
#else
  printf("stats: drops=%" PRIu64 " k_rexmit=%" PRIu64 " ecn=%" PRIu64
         " acks=%" PRIu64 "\n",
         drops, kernel_rexmit, ecn_marked, acks);
#endif

  fflush(stdout);
}

/* Run budget control on a dedicated lcore so slow-path control work cannot
 * delay updates. */
static int budget_thread(void *arg)
{
  const uint64_t tsc_per_us = util_timeout_tsc_per_us();
  const uint64_t update_period_tsc = config.bu_update_freq * tsc_per_us;
  const uint64_t print_period_tsc = 1000000ULL * tsc_per_us;
  uint64_t next_update = 0;
  uint64_t next_print;
  uint64_t now;

  (void) arg;

  {
    char name[17];
    snprintf(name, sizeof(name), "stcp-budget");
    pthread_setname_np(pthread_self(), name);
  }

  now = util_rdtsc();
  next_print = now + print_period_tsc;

  if (update_period_tsc != 0)
  {
    next_update = now + update_period_tsc;
  }

  budget_update(now);

  while (exited == 0)
  {
    now = util_rdtsc();

    if (update_period_tsc == 0 || now >= next_update)
    {
      budget_update(now);
      if (update_period_tsc != 0)
      {
        budget_thread_advance_deadline(&next_update, update_period_tsc, now);
      }
    }

    if (now >= next_print)
    {
      slowpath_print_stats();
      budget_thread_advance_deadline(&next_print, print_period_tsc, now);
    }

    if ((update_period_tsc == 0 || now < next_update) && now < next_print)
    {
      rte_pause();
    }
  }

  return 0;
}

static void budget_thread_advance_deadline(uint64_t *deadline,
    uint64_t period_tsc, uint64_t now)
{
  do
  {
    *deadline += period_tsc;
  }
  while (*deadline <= now);
}

static void slowpath_block(uint32_t cur_ts)
{
  int n, i, ret, timeout_ms;
  struct epoll_event event[2];
  uint64_t val;
  uint32_t cc_timeout = cc_next_ts(cur_ts),
           util_timeout = util_timeout_next(&timeout_mgr, cur_ts),
           timeout_us;

  if (cc_timeout != -1U && util_timeout != -1U)
  {
    timeout_us = TAS_MIN(cc_timeout, util_timeout);
  }
  else if (cc_timeout != -1U)
  {
    timeout_us = util_timeout;
  }
  else
  {
    timeout_us = cc_timeout;
  }
  if (timeout_us != -1U)
  {
    timeout_ms = timeout_us / 1000;
  }
  else
  {
    timeout_ms = -1;
  }

  // Deal with load management
  if (timeout_ms == -1 || timeout_ms > 1000)
  {
    timeout_ms = 10;
  }

again:
  n = epoll_wait(epfd, event, 2, timeout_ms);
  if (n == -1 && errno == EINTR)
  {
    /* To support attaching GDB */
    goto again;
  }
  else if (n == -1)
  {
    perror("slowpath_block: epoll_wait failed");
    abort();
  }

  for (i = 0; i < n; i++)
  {
    assert(event[i].data.fd == kernel_notifyfd);
    ret = read(kernel_notifyfd, &val, sizeof(uint64_t));
    if ((ret > 0 && ret != sizeof(uint64_t)) ||
        (ret < 0 && errno != EAGAIN && errno != EWOULDBLOCK))
    {
      perror("slowpath_block: read failed");
      abort();
    }
  }
}

static void timeout_trigger(struct timeout *to, uint8_t type, void *opaque)
{
  switch (type)
  {
  case TO_ARP_REQ:
    arp_timeout(to, type);
    break;

  case TO_TCP_HANDSHAKE:
  case TO_TCP_RETRANSMIT:
  case TO_TCP_CLOSED:
    tcp_timeout(to, type);
    break;

  default:
    fprintf(stderr, "Unknown timeout type: %u\n", type);
    abort();
  }
}

static void signal_tas_ready(void)
{
  uint64_t x;

  printf("TAS ready\n");
  fflush(stdout);

  x = 1;
  if (config.ready_fd >= 0 &&
      write(config.ready_fd, &x, sizeof(x)) < 0)
  {
    perror("TAS signal: ready fd write failed");
    /* proceeed */
  }
}
