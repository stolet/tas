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

#ifndef FASTEMU_H_
#define FASTEMU_H_


#include "tcp_common.h"

/*****************************************************************************/
/* fast_kernel.c */
int fast_kernel_poll(struct dataplane_context *ctx,
    struct network_buf_handle *nbh, uint32_t ts);
void fast_kernel_packet(struct dataplane_context *ctx,
    struct network_buf_handle *nbh, void *fsp);

/* fast_appctx.c */
void fast_appctx_poll_pf_all(struct dataplane_context *ctx);
void fast_appctx_poll_pf_active(struct dataplane_context *ctx);
int fast_appctx_poll_fetch_all(struct dataplane_context *ctx, uint16_t max,
        unsigned *total, void *aqes[BATCH_SIZE]);
int fast_appctx_poll_fetch_active(struct dataplane_context *ctx, uint16_t max,
        unsigned *total, int *n_rem, struct polled_context *rem_apps[BATCH_SIZE], 
        void *aqes[BATCH_SIZE]);
int fast_appctx_poll_bump(struct dataplane_context *ctx, void *pqe,
    struct network_buf_handle *nbh, uint32_t ts);

int fast_appctx_poll(struct dataplane_context *ctx, uint32_t id,
    struct network_buf_handle *nbh, uint32_t ts);
int fast_actx_rxq_alloc(struct dataplane_context *ctx,
    struct flextcp_pl_appctx *actx, struct flextcp_pl_arx **arx, uint16_t vmid);
void fast_actx_rxq_probe_all(struct dataplane_context *ctx);
void fast_actx_rxq_probe_active(struct dataplane_context *ctx);
void remove_ctxs_from_active(struct dataplane_context *ctx, 
    struct polled_context *ctxs[BATCH_SIZE], int n);
void enqueue_ctx_to_active(struct polled_vm *act_vm, uint32_t cid); 
void remove_ctx_from_active(struct polled_vm *act_vm, 
    struct polled_context *act_ctx);
void enqueue_vm_to_active(struct dataplane_context *ctx, uint16_t vmid);
void remove_vm_from_active(struct dataplane_context *ctx, 
    struct polled_vm *act_vm);

/* fast_flows.c */
void fast_flows_qman_pf(struct dataplane_context *ctx, uint32_t *queues,
    uint16_t n);
void fast_flows_qman_pfbufs(struct dataplane_context *ctx, uint32_t *queues,
    uint16_t n);
int fast_flows_qman(struct dataplane_context *ctx, uint32_t vm_id,
    uint32_t queue, struct network_buf_handle *nbh, uint32_t ts);
int fast_flows_qman_fwd(struct dataplane_context *ctx,
    struct flextcp_pl_flowst *fs);
int fast_flows_packet(struct dataplane_context *ctx,
    struct network_buf_handle *nbh, void *fsp, struct tcp_opts *opts,
    uint32_t ts);
int fast_flows_packet_gre(struct dataplane_context *ctx,
    struct network_buf_handle *nbh, void *fs, struct tcp_opts *opts,
    uint32_t ts);
void fast_flows_packet_fss(struct dataplane_context *ctx,
    struct network_buf_handle **nbhs, void **fss, uint16_t n);
void fast_flows_packet_fss_gre(struct dataplane_context *ctx,
    struct network_buf_handle **nbhs, void **fss, uint16_t n);
void fast_flows_packet_parse(struct dataplane_context *ctx,
    struct network_buf_handle **nbhs, void **fss, struct tcp_opts *tos,
    uint16_t n);
void fast_flows_packet_parse_gre(struct dataplane_context *ctx,
    struct network_buf_handle **nbhs, void **fss, struct tcp_opts *tos,
    uint16_t n);
void fast_flows_packet_pfbufs(struct dataplane_context *ctx,
    void **fss, uint16_t n);
void fast_flows_kernelxsums(struct network_buf_handle *nbh,
    struct pkt_tcp *p);
void fast_flows_kernelxsums_gre(struct network_buf_handle *nbh,
    struct pkt_gre *p);

int fast_flows_bump(struct dataplane_context *ctx, uint32_t flow_id,
    uint16_t bump_seq, uint32_t rx_tail, uint32_t tx_head, uint8_t flags,
    struct network_buf_handle *nbh, uint32_t ts);
void fast_flows_winretransmit(struct dataplane_context *ctx, uint32_t flow_id,
    struct network_buf_handle *nbh, uint32_t ts);
void fast_flows_retransmit(struct dataplane_context *ctx, uint32_t flow_id);

/* fastemu.c */
uint8_t bufcache_prealloc(struct dataplane_context *ctx, uint16_t num,
                                struct network_buf_handle ***handles);
void bufcache_alloc(struct dataplane_context *ctx, uint16_t num);
                                
/*****************************************************************************/
/* Helpers */

static inline void tx_send(struct dataplane_context *ctx,
    struct network_buf_handle *nbh, uint16_t off, uint16_t len)
{
  uint32_t i = ctx->tx_num;

  if (i >= TXBUF_SIZE) {
    fprintf(stderr, "tx_send: transmit buffer full, unexpected\n");
    abort();
  }

  network_buf_setoff(nbh, off);
  network_buf_setlen(nbh, len);
  ctx->tx_handles[i] = nbh;
  ctx->tx_num = i + 1;
}

static inline uint16_t tx_xsum_enable(struct network_buf_handle *nbh,
    struct ip_hdr *iph, beui32_t ip_s, beui32_t ip_d, uint16_t l3_paylen)
{
  return network_buf_tcpxsums(nbh, sizeof(struct eth_hdr), sizeof(*iph), iph,
      ip_s, ip_d, IP_PROTO_TCP, l3_paylen);
}

static inline uint16_t tx_gre_xsum_enable(struct network_buf_handle *nbh,
    struct ip_hdr *in_iph, struct ip_hdr *out_iph, uint16_t l3_paylen)
{
  beui32_t ip_s = in_iph->src;
  beui32_t ip_d = in_iph->dest;

  return network_buf_grexsums(nbh, 
      sizeof(struct eth_hdr), sizeof(*in_iph), sizeof(*out_iph),
      ip_s, ip_d, IP_PROTO_TCP, l3_paylen);
}

static inline void arx_cache_add(struct dataplane_context *ctx, uint16_t ctx_id,
    uint16_t vmid, uint64_t opaque, uint32_t rx_bump, uint32_t rx_pos, 
    uint32_t tx_bump, uint16_t type_flags)
{
  uint16_t id = ctx->arx_num++;

  ctx->arx_ctx[id] = ctx_id;
  ctx->arx_vm[id] = vmid;
  ctx->arx_cache[id].type = type_flags & 0xff;
  ctx->arx_cache[id].msg.connupdate.opaque = opaque;
  ctx->arx_cache[id].msg.connupdate.rx_bump = rx_bump;
  ctx->arx_cache[id].msg.connupdate.rx_pos = rx_pos;
  ctx->arx_cache[id].msg.connupdate.tx_bump = tx_bump;
  ctx->arx_cache[id].msg.connupdate.flags = type_flags >> 8;
}

#endif /* ndef FASTEMU_H_ */
