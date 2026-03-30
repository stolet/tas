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


void fast_appctx_poll_pf(struct dataplane_context *ctx, 
    uint16_t vmid, uint32_t ctxid)
{
  struct flextcp_pl_appctx *actx = &fp_state->appctx[ctx->id][vmid][ctxid];
  rte_prefetch0(dma_pointer(actx->tx_base + actx->tx_head, 1, vmid));
}

int fast_appctx_poll_fetch(struct dataplane_context *ctx,
    uint32_t vmid, uint16_t actxid,
    void **pqe, uint8_t spend_budget)
{
  struct flextcp_pl_appctx *actx = &fp_state->appctx[ctx->id][vmid][actxid];
  struct flextcp_pl_atx *atx;
  uint8_t type;
  uint32_t flow_id;
  uint32_t tx_head;

  /* stop if context is not in use */
  if (actx->tx_len == 0)
    return -1;

  tx_head = actx->tx_head;
  atx = dma_pointer(actx->tx_base + tx_head, sizeof(*atx), vmid);

  type = atx->type;
  MEM_BARRIER();

  if (type == 0) 
  {
    return -1;
  } 
  else if (type != FLEXTCP_PL_ATX_CONNUPDATE) 
  {
    fprintf(stderr, "fast_appctx_poll: unknown type: %u id=%u\n", type, actxid);
    abort();
  }
  *pqe = atx;

  /* update RX/TX queue pointers for connection */
  flow_id = atx->msg.connupdate.flow_id;
  if (flow_id >= FLEXNIC_PL_FLOWST_NUM) 
  {
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

#ifdef BUDGET_DEBUG_STATS
  if (!spend_budget) 
  {
    ctx->budget_debug_work_conserving_vm[vm_id] += atx->msg.connupdate.tx_bump;
    ctx->budget_debug_work_conserving_total += atx->msg.connupdate.tx_bump;
  }
#endif

  return 0;
}

int fast_appctx_poll_bump(struct dataplane_context *ctx, void *pqe,
    struct network_buf_handle *nbh, int *vmid, uint32_t ts)
{
  int ret;
  
  struct flextcp_pl_atx *atx = pqe;
  int flow_id = atx->msg.connupdate.flow_id;

  ret = fast_flows_bump(ctx, flow_id, vmid,
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

int fast_actx_rxq_probe(struct dataplane_context *ctx, 
    uint16_t vmid, uint32_t actxid)
{
  struct flextcp_pl_appctx *actx = &fp_state->appctx[ctx->id][vmid][actxid];
  struct flextcp_pl_arx *parx;
  uint32_t pos, i;
  
  if (actx->rx_avail > actx->rx_len / 2) {
    return -1;
  }

  pos = actx->rx_head + actx->rx_avail;
  if (pos >= actx->rx_len)
    pos -= actx->rx_len;

  i = 0;
  while (actx->rx_avail < actx->rx_len && i < 2 * BATCH_SIZE) 
  {
    parx = dma_pointer(actx->rx_base + pos, sizeof(*parx), vmid);

    if (parx->type != 0) 
      break;

    actx->rx_avail += sizeof(*parx);
    pos += sizeof(*parx);
    if (pos >= actx->rx_len)
      pos -= actx->rx_len;
    i++;

    MEM_BARRIER();
  }

  return 0;
}