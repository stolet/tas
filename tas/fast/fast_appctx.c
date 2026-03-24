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
#include <rte_config.h>

#include <utils.h>
#include <utils_sync.h>
#include <tas.h>
#include <tas_memif.h>

#include "internal.h"
#include "fastemu.h"

void fast_appctx_poll_pf_active_vm(struct dataplane_context *ctx, uint32_t vmid);


void fast_appctx_poll_fetch_active_ctx(struct dataplane_context *ctx,
  struct polled_context *act_ctx, uint16_t *k, uint16_t max,
  unsigned *total, int *n_rem, struct polled_context *rem_ctxs[BATCH_SIZE],
  void *aqes[BATCH_SIZE], bool spend_budget);
void fast_appctx_poll_fetch_active_vm(struct dataplane_context *ctx, 
    struct polled_vm *act_vm, uint16_t *k, uint16_t max,
    unsigned *total, int *n_rem, struct polled_context *rem_ctxs[BATCH_SIZE],
    void *aqes[BATCH_SIZE], bool spend_budget);
int fast_appctx_poll_fetch(struct dataplane_context *ctx,
    struct flextcp_pl_appctx *actx, uint32_t actx_id, uint16_t vm_id,
    void **pqe, bool spend_budget);

void fast_actx_rxq_probe_active_vm(struct dataplane_context *ctx, 
    struct polled_vm *act_vm);
int fast_actx_rxq_probe(struct flextcp_pl_appctx *actx, uint16_t vm_id);

static inline struct flextcp_pl_appctx *fast_appctx_lookup(
    struct dataplane_context *ctx, uint16_t vmid, uint32_t cid)
{
  return &fp_state->appctx[ctx->id][vmid][cid];
}
    

void inline fast_appctx_poll_pf_active_vm(struct dataplane_context *ctx, uint32_t vmid)
{
  uint32_t cid;
  struct polled_vm *act_vm;
  act_vm = &ctx->polled_vms[vmid];

  cid = act_vm->act_ctx_head;
  do {
    fast_appctx_poll_pf(fast_appctx_lookup(ctx, vmid, cid), vmid);
    cid = act_vm->ctxs[cid].next;
  } while(cid != act_vm->act_ctx_head);
}

void inline fast_appctx_poll_pf_active(struct dataplane_context *ctx)
{
  uint32_t vmid;

  vmid = ctx->act_head;
  do {
    fast_appctx_poll_pf_active_vm(ctx, vmid);
    vmid = ctx->polled_vms[vmid].next;
  } while (vmid != ctx->act_head);
} 

void inline fast_appctx_poll_pf(struct flextcp_pl_appctx *actx, uint16_t vmid)
{
  rte_prefetch0(dma_pointer(actx->tx_base + actx->tx_head, 1, vmid));
}

void fast_appctx_poll_fetch_active_ctx(struct dataplane_context *ctx,
  struct polled_context *act_ctx, uint16_t *k, uint16_t max,
  unsigned *total, int *n_rem, struct polled_context *rem_ctxs[BATCH_SIZE],
  void *aqes[BATCH_SIZE], bool spend_budget)
{
  int ret;
  unsigned i_b;
  struct flextcp_pl_appctx *actx;

  actx = fast_appctx_lookup(ctx, act_ctx->vmid, act_ctx->id);

  for (i_b = 0; i_b < BATCH_SIZE && *k < max; i_b++) 
  {
    ret = fast_appctx_poll_fetch(ctx, actx, act_ctx->id, act_ctx->vmid,
        &aqes[*k], spend_budget);
    if (ret == 0)
    {
      *k = *k + 1;
      act_ctx->null_rounds = 0;
    } else
    {
      act_ctx->null_rounds = act_ctx->null_rounds == MAX_NULL_ROUNDS ? 
          MAX_NULL_ROUNDS : act_ctx->null_rounds + 1;

      if (act_ctx->null_rounds >= MAX_NULL_ROUNDS)
      {
        rem_ctxs[*n_rem] = act_ctx;
        *n_rem = *n_rem + 1;
      }
      break;
    }
    *total = *total + 1;
  }
}

void inline fast_appctx_poll_fetch_active_vm(struct dataplane_context *ctx, 
    struct polled_vm *act_vm, uint16_t *k, uint16_t max,
    unsigned *total, int *n_rem, struct polled_context *rem_ctxs[BATCH_SIZE],
    void *aqes[BATCH_SIZE], bool spend_budget)
{
  uint32_t cid;
  struct polled_context *act_ctx;

  cid = act_vm->act_ctx_head;
  do  {
    act_ctx = &act_vm->ctxs[cid];
    
    fast_appctx_poll_fetch_active_ctx(ctx, act_ctx, k, max, 
        total, n_rem, rem_ctxs, aqes, spend_budget);
        
    cid = act_ctx->next;
  } while(cid != act_vm->act_ctx_head && *k < max);

  act_vm->act_ctx_head = act_vm->ctxs[act_vm->act_ctx_head].next;
  act_vm->act_ctx_tail = act_vm->ctxs[act_vm->act_ctx_tail].next;
}

int fast_appctx_poll_fetch_active(struct dataplane_context *ctx, uint16_t max,
    unsigned *total, int *n_rem, struct polled_context *rem_ctxs[BATCH_SIZE], 
    void *aqes[BATCH_SIZE])
{
  uint16_t temp_k, k = 0;
  uint32_t vmid;
  struct polled_vm *act_vm;

  int oob_i = 0, oob_n;
  int oob_vms[FLEXNIC_PL_VMST_NUM];
  
  vmid = ctx->act_head;
  do {
    act_vm = &ctx->polled_vms[vmid];

    if (dataplane_budget_available(ctx, vmid))
    {
      fast_appctx_poll_fetch_active_vm(ctx, act_vm, &k, max, total, 
          n_rem, rem_ctxs, aqes, true);
    } else
    {
      oob_vms[oob_i] = vmid;
      oob_i++;
    }

    vmid = ctx->polled_vms[vmid].next; 
  } while (vmid != ctx->act_head && k < max);

  oob_n = oob_i;
  temp_k = k;
  for (oob_i = 0; oob_i < oob_n && temp_k == 0; oob_i++)
  {
    vmid = oob_vms[oob_i];
    act_vm = &ctx->polled_vms[vmid];
    if (k < max)
    {
      fast_appctx_poll_fetch_active_vm(ctx, act_vm, &k, max, total, n_rem, 
          rem_ctxs, aqes, false);
    }
  }

  return k;
}

int fast_appctx_poll_fetch(struct dataplane_context *ctx,
    struct flextcp_pl_appctx *actx, uint32_t actx_id, uint16_t vm_id,
    void **pqe, bool spend_budget)
{
  struct flextcp_pl_atx *atx;
  uint8_t type;
  uint32_t flow_id;
  uint32_t tx_head;

  /* stop if context is not in use */
  if (actx->tx_len == 0)
    return -1;

  tx_head = actx->tx_head;
  atx = dma_pointer(actx->tx_base + tx_head, sizeof(*atx), vm_id);

  type = atx->type;
  MEM_BARRIER();

  if (type == 0) {
    return -1;
  } else if (type != FLEXTCP_PL_ATX_CONNUPDATE) {
    fprintf(stderr, "fast_appctx_poll: unknown type: %u id=%u\n", type,
        actx_id);
    abort();
  }
  *pqe = atx;

  /* update RX/TX queue pointers for connection */
  flow_id = atx->msg.connupdate.flow_id;
  if (flow_id >= FLEXNIC_PL_FLOWST_NUM) {
    fprintf(stderr, "fast_appctx_poll: invalid flow id=%u\n", flow_id);
    abort();
  }

  void *fs = &fp_state->flowst[flow_id];
  rte_prefetch0(fs);
  rte_prefetch0(fs + 64);
 
  tx_head += sizeof(*atx);
  if (tx_head >= actx->tx_len)
    tx_head -= actx->tx_len;
  actx->tx_head = tx_head;

  if (spend_budget) {
    dataplane_budget_account(ctx, vm_id, atx->msg.connupdate.tx_bump);
#ifdef BUDGET_DEBUG_STATS
  } else {
    ctx->budget_debug_work_conserving_vm[vm_id] += atx->msg.connupdate.tx_bump;
    ctx->budget_debug_work_conserving_total += atx->msg.connupdate.tx_bump;
#endif
  }

  return 0;
}

int inline fast_appctx_poll_bump(struct dataplane_context *ctx, void *pqe,
    struct network_buf_handle *nbh, uint32_t ts)
{
  int ret;
  
  struct flextcp_pl_atx *atx = pqe;
  int flow_id = atx->msg.connupdate.flow_id;

  ret = fast_flows_bump(ctx, flow_id,
      atx->msg.connupdate.bump_seq, atx->msg.connupdate.rx_bump,
      atx->msg.connupdate.tx_bump, atx->msg.connupdate.flags, nbh, ts);

  if (ret != 0)
    ret = 1;

  MEM_BARRIER();
  atx->type = 0;
  return ret;
}

void fast_actx_rxq_pf(struct dataplane_context *ctx,
    struct flextcp_pl_appctx *actx, uint16_t vmid)
{
  rte_prefetch0(dma_pointer(actx->rx_base + actx->rx_head,
        sizeof(struct flextcp_pl_arx), vmid));
}

int fast_actx_rxq_alloc(struct dataplane_context *ctx,
    struct flextcp_pl_appctx *actx, struct flextcp_pl_arx **arx, uint16_t vmid)
{
  struct flextcp_pl_arx *parx;
  uint32_t rxnhead;
  int ret = 0;

  if (actx->rx_avail == 0) {
    return -1;
  }

  MEM_BARRIER();
  parx = dma_pointer(actx->rx_base + actx->rx_head, sizeof(*parx), vmid);

  rxnhead = actx->rx_head + sizeof(*parx);
  if (rxnhead >= actx->rx_len) {
    rxnhead -= actx->rx_len;
  }
  actx->rx_head = rxnhead;
  actx->rx_avail -= sizeof(*parx);

  *arx = parx;
  return ret;
}

int fast_actx_rxq_probe(struct flextcp_pl_appctx *actx, uint16_t vmid)
{
  struct flextcp_pl_arx *parx;
  uint32_t pos, i;
  if (actx->rx_avail > actx->rx_len / 2) {
    return -1;
  }

  pos = actx->rx_head + actx->rx_avail;
  if (pos >= actx->rx_len)
    pos -= actx->rx_len;

  i = 0;
  while (actx->rx_avail < actx->rx_len && i < 2 * BATCH_SIZE) {
    parx = dma_pointer(actx->rx_base + pos, sizeof(*parx), vmid);

    if (parx->type != 0) {
      break;
    }

    actx->rx_avail += sizeof(*parx);
    pos += sizeof(*parx);
    if (pos >= actx->rx_len)
      pos -= actx->rx_len;
    i++;

    MEM_BARRIER();
  }

  return 0;
}

void inline fast_actx_rxq_probe_active_vm(struct dataplane_context *ctx, 
    struct polled_vm *act_vm)
{
  uint32_t cid, vmid;

  cid = act_vm->act_ctx_head;
  vmid = act_vm->id;
  do {
    fast_actx_rxq_probe(fast_appctx_lookup(ctx, vmid, cid), vmid);
    cid = act_vm->ctxs[cid].next;
  } while(cid != act_vm->act_ctx_head); 
}

void inline fast_actx_rxq_probe_active(struct dataplane_context *ctx)
{
  uint32_t vmid;
  struct polled_vm *act_vm;

  vmid = ctx->act_head;
  do {
    act_vm = &ctx->polled_vms[vmid];
    fast_actx_rxq_probe_active_vm(ctx, act_vm);
    vmid = ctx->polled_vms[vmid].next;
  } while (vmid != ctx->act_head);
}

/*****************************************************************************/
/* Manages active vms and context rings */

void remove_ctxs_from_active(struct dataplane_context *ctx, 
    struct polled_context *ctxs[BATCH_SIZE], int n)
{
  int i;
  uint32_t vmid, cid;
  struct polled_vm *p_vm;

  for (i = 0; i < n; i++)
  {
    cid = ctxs[i]->id;
    vmid = ctxs[i]->vmid;
    p_vm = &ctx->polled_vms[vmid];
    remove_ctx_from_active(p_vm, &p_vm->ctxs[cid]);

    if (p_vm->act_ctx_head == IDXLIST_INVAL)
    {
      remove_vm_from_active(ctx, p_vm);
    }
  }
}

void enqueue_ctx_to_active(struct polled_vm *act_vm, uint32_t cid) 
{
  if (act_vm->act_ctx_tail == IDXLIST_INVAL)
  {
    act_vm->act_ctx_tail = act_vm->act_ctx_head = cid;
    act_vm->ctxs[cid].prev = act_vm->act_ctx_tail;
    act_vm->ctxs[cid].next = act_vm->act_ctx_head;
    act_vm->ctxs[cid].flags |= FLAG_ACTIVE;
    return;
  }

  act_vm->ctxs[act_vm->act_ctx_tail].next = cid;
  act_vm->ctxs[act_vm->act_ctx_head].prev = cid;
  act_vm->ctxs[cid].prev = act_vm->act_ctx_tail;
  act_vm->ctxs[cid].next = act_vm->act_ctx_head;
  act_vm->ctxs[cid].flags |= FLAG_ACTIVE;
  act_vm->act_ctx_tail = cid;
}

void remove_ctx_from_active(struct polled_vm *act_vm, 
    struct polled_context *act_ctx)
{
  if (act_vm->act_ctx_tail == act_vm->act_ctx_head)
  {
    act_vm->act_ctx_head = act_vm->act_ctx_tail = IDXLIST_INVAL;
    act_ctx->next = IDXLIST_INVAL;
    act_ctx->prev = IDXLIST_INVAL;
    act_ctx->flags &= ~FLAG_ACTIVE;
    act_ctx->null_rounds = 0;
    return;
  }
  
  /* element is tail */
  if (act_ctx->next == act_vm->act_ctx_head)
  {
    act_vm->act_ctx_tail = act_ctx->prev;
  } else if (act_ctx->prev == act_vm->act_ctx_tail) /* element is head */
  {
    act_vm->act_ctx_head = act_ctx->next;
  }
  
  act_vm->ctxs[act_ctx->prev].next = act_ctx->next;
  act_vm->ctxs[act_ctx->next].prev = act_ctx->prev;
  act_ctx->next = IDXLIST_INVAL;
  act_ctx->prev = IDXLIST_INVAL;
  act_ctx->flags &= ~FLAG_ACTIVE;
  act_ctx->null_rounds = 0;
}

void enqueue_vm_to_active(struct dataplane_context *ctx, uint16_t vmid)
{
  if (ctx->act_tail == IDXLIST_INVAL)
  {
    ctx->act_tail = ctx->act_head = vmid;
    ctx->polled_vms[vmid].prev = ctx->act_tail;
    ctx->polled_vms[vmid].next = ctx->act_head;
    ctx->polled_vms[vmid].flags |= FLAG_ACTIVE;
    return;
  }

  ctx->polled_vms[ctx->act_tail].next = vmid;
  ctx->polled_vms[ctx->act_head].prev = vmid;
  ctx->polled_vms[vmid].prev = ctx->act_tail;
  ctx->polled_vms[vmid].next = ctx->act_head;
  ctx->polled_vms[vmid].flags |= FLAG_ACTIVE;
  ctx->act_tail = vmid;
 }

void remove_vm_from_active(struct dataplane_context *ctx, 
    struct polled_vm *act_vm)
{
  /* one vm in ring */
  if (ctx->act_tail == ctx->act_head)
  {
    ctx->act_head = ctx->act_tail = IDXLIST_INVAL;
    act_vm->next = IDXLIST_INVAL;
    act_vm->prev = IDXLIST_INVAL;
    act_vm->flags &= ~FLAG_ACTIVE;
    return;
  }
  
  /* element is tail */
  if (act_vm->next == ctx->act_head)
  {
    ctx->act_tail = act_vm->prev;
  } else if (act_vm->prev == ctx->act_tail) /* element is head */
  {
    ctx->act_head = act_vm->next;
  }
  
  ctx->polled_vms[act_vm->prev].next = act_vm->next;
  ctx->polled_vms[act_vm->next].prev = act_vm->prev;
  act_vm->next = IDXLIST_INVAL;
  act_vm->prev = IDXLIST_INVAL;
  act_vm->flags &= ~FLAG_ACTIVE;
}
