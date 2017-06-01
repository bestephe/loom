#include <rte_tcp.h>
#include <rte_udp.h>
#include <rte_byteorder.h>
#include <rte_malloc.h>

#include "features.h"
#include "module.h"
#include "checksum.h"
#include "port.h"
#include "worker.h"

#define FRAME_SIZE	1514		/* TODO: check pport MTU */

static void batch_push(struct module *this, struct pkt_batch *batch,
		       struct snbuf *pkt)
{
	batch_add(batch, pkt);

	if (batch_full(batch)) {
		run_next_module(this, batch);
		batch_clear(batch);
	}
}

typedef enum {
	FLOW_UNSUPPORTED = 0,
	FLOW_RAW,
	FLOW_VXLAN,
} flow_type_t;

/* also returns (inner) IP and TCP offsets */
static flow_type_t check_type(struct snbuf *pkt, 
			      uint16_t *ip_offset, uint16_t *tcp_offset)
{
	struct ipv4_hdr *ip;
	struct tcp_hdr *tcp;
	struct udp_hdr *udp;

	ip = snb_ipv4(pkt);
	tcp = snb_tcp(pkt);

	if (!ip)
		goto fail;

	if (tcp) {
		*ip_offset = pkt->l3_offset;
		*tcp_offset = pkt->l4_offset;
		return FLOW_RAW;
	}
	
	udp = snb_udp(pkt);
	if (!udp)
		goto fail;

	/* TCP in VXLAN? */
	if (udp->dst_port == rte_cpu_to_be_16(VXLAN_PORT)) {
		struct ether_hdr *eth;
		uint16_t l2_offset;

		/* TODO: packet sanity check (reuse parse.c?) */

		/* 8 for vxlan header */
		l2_offset = pkt->l4_offset + sizeof(struct udp_hdr) + 8;
		eth = (struct ether_hdr *)(snb_head_data(pkt) + l2_offset);
		if (eth->ether_type != rte_cpu_to_be_16(ETHER_TYPE_IPv4))
			goto fail;

		*ip_offset = l2_offset + 14;
		ip = (struct ipv4_hdr *)(snb_head_data(pkt) + *ip_offset);
		if (ip->next_proto_id != IPPROTO_TCP)
			goto fail;

		*tcp_offset = *ip_offset + ((ip->version_ihl & 0x0f) << 2);
		return FLOW_VXLAN;
	}

fail:
	return FLOW_UNSUPPORTED;
}

static void do_tso(struct module *this, struct pkt_batch *new_batch,
		   struct snbuf *pkt)
{
	flow_type_t type;
	uint16_t ip_offset;
	uint16_t tcp_offset;
	uint16_t tcp_hdrlen;
	uint16_t payload_offset;
	uint32_t seq;

	struct tcp_hdr *tcp;

	int org_frame_len = snb_total_len(pkt);
	int max_seg_size;
	int seg_size;
	int i;

	assert(snb_is_linear(pkt));

	if (org_frame_len <= FRAME_SIZE) {
		batch_push(this, new_batch, pkt);
		return;
	}

	type = check_type(pkt, &ip_offset, &tcp_offset);
	assert(type != FLOW_UNSUPPORTED);

	tcp = (struct tcp_hdr *)(snb_head_data(pkt) + tcp_offset);
	tcp_hdrlen = (tcp->data_off >> 4) * 4;
	payload_offset = tcp_offset + tcp_hdrlen;

	seq = rte_be_to_cpu_32(tcp->sent_seq);
	
	max_seg_size = FRAME_SIZE - payload_offset;

	for (i = payload_offset; i < org_frame_len; i += max_seg_size) {
		struct snbuf *new_pkt;

		struct ipv4_hdr *ip;
		struct tcp_hdr *tcp;
		
		uint16_t new_ip_total_len;

		int first = (i == payload_offset);
		int last = (i + max_seg_size >= org_frame_len);

		seg_size = RTE_MIN(org_frame_len - i, max_seg_size);

		new_pkt = snb_alloc_with_metadata(SNBUF_PFRAME, pkt);

		/* copy the headers */
		rte_memcpy(snb_append(new_pkt, payload_offset),
				snb_head_data(pkt), payload_offset);

		ip = (struct ipv4_hdr *)(snb_head_data(new_pkt) + ip_offset);
		new_ip_total_len = (payload_offset - ip_offset) + seg_size;
		ip->total_length = rte_cpu_to_be_16(new_ip_total_len);
		ip->hdr_checksum = rte_cpu_to_be_16(0);
		ip->hdr_checksum = ip_csum(ip, (tcp_offset - ip_offset) >> 2);

		tcp = (struct tcp_hdr *)(snb_head_data(new_pkt) + tcp_offset);
		tcp->sent_seq = rte_cpu_to_be_32(seq);
		seq += max_seg_size;

		/* CWR only for the first packet */
		if (!first)
			tcp->tcp_flags &= 0x7f;	

		/* PSH and FIN only for the last packet */
		if (!last)
			tcp->tcp_flags &= 0xf6;

		/* TCP checksum update */
		new_pkt->tx.csum_start = tcp_offset;
		new_pkt->tx.csum_dest = tcp_offset + 
				offsetof(struct tcp_hdr, cksum);

		tcp->cksum = ~csum_tcpudp_magic(ip->src_addr, ip->dst_addr, 
				tcp_hdrlen + seg_size, IPPROTO_TCP);

		if (type == FLOW_VXLAN) {
			/* update the outer headers */
			struct udp_hdr *udp;

			ip = snb_ipv4(new_pkt);
			new_ip_total_len = (payload_offset - new_pkt->l3_offset) 
					+ seg_size;
			ip->total_length = rte_cpu_to_be_16(new_ip_total_len);
			ip->hdr_checksum = rte_cpu_to_be_16(0);
			ip->hdr_checksum = ip_csum(ip, (ip->version_ihl & 0x0f));

			udp = snb_udp(new_pkt);
			udp->dgram_len = rte_cpu_to_be_16(snb_total_len(new_pkt) - 
					new_pkt->l4_offset);
			udp->dgram_cksum = rte_cpu_to_be_16(0);

			/* UDP zero checksum */
			new_pkt->tx.csum_start = SN_TX_CSUM_DONT;
		}
		
/* zero-copy TSO implentation */
#if SN_TSO_SG
{
		struct rte_mbuf *data_mbuf;

		data_mbuf = rte_pktmbuf_alloc(pframe_pool[current_sid]);
		assert(data_mbuf);

		assert(snb_is_linear(pkt));
		
		rte_pktmbuf_attach(data_mbuf, &pkt->mbuf);
		data_mbuf->data_off += i;
		data_mbuf->data_len = seg_size;
		
		/* attach the indirect payload segment to the header segment */
		new_pkt->mbuf.next = data_mbuf;
		new_pkt->mbuf.nb_segs++;
		new_pkt->mbuf.pkt_len += seg_size;
}
#else
		/* copy the payload */
		rte_memcpy(snb_append(new_pkt, seg_size),
				snb_head_data(pkt) + i, seg_size);
#endif
		batch_push(this, new_batch, new_pkt);
	}

	snb_free(pkt);
}

static void tso_op_process_batch(struct module *this,
				 struct pkt_batch *batch)
{
	struct pkt_batch new_batch;
	int i;

	batch_clear(&new_batch);

	for (i = 0; i < batch->cnt; i++) {
		struct snbuf *pkt = batch->pkts[i];

		do_tso(this, &new_batch, pkt);
	}

	run_next_module(this, &new_batch);
}

const struct module_ops tso_ops = {
	.process_batch	= tso_op_process_batch,
};

struct lro_flow {
	struct snbuf *pkt;	/* NULL if empty */
	struct rte_mbuf *last_mbuf;
	uint64_t usec;

	uint32_t src_addr;
	uint32_t dst_addr;
	uint16_t src_port;
	uint16_t dst_port;

	uint32_t next_seq;	/* in host order */

	/* Offset of (inner, if encapsulated) IP/TCP. */
	uint16_t ip_offset;
	uint16_t tcp_offset;
	flow_type_t type;
};

#define MAX_LRO_FLOWS	16

static void lro_flush_flow(struct module *this, struct pkt_batch *batch,
		       struct lro_flow *flow)
{
	struct snbuf *pkt = flow->pkt;
	struct ipv4_hdr *ip = snb_ipv4(pkt);

	ip->total_length = rte_cpu_to_be_16(snb_total_len(pkt) - pkt->l3_offset);

	/* Linux IP stack checks IP checksum regardless of offloading */
	ip->hdr_checksum = rte_cpu_to_be_16(0);
	ip->hdr_checksum = ip_csum(ip, (ip->version_ihl & 0x0f));

	if (flow->type == FLOW_VXLAN) {
		/* Outer UDP header */
		struct udp_hdr *udp = snb_udp(pkt);
		udp->dgram_len = rte_cpu_to_be_16(snb_total_len(pkt) - pkt->l4_offset);

		/* Inner IP header */
		ip = (struct ipv4_hdr *)(snb_head_data(pkt) + flow->ip_offset);

		ip->total_length = rte_cpu_to_be_16(snb_total_len(pkt) - flow->ip_offset);

		/* Linux IP stack checks IP checksum regardless of offloading */
		ip->hdr_checksum = rte_cpu_to_be_16(0);
		ip->hdr_checksum = ip_csum(ip, (ip->version_ihl & 0x0f));
		
		pkt->rx.csum_state = SN_RX_CSUM_CORRECT_ENCAP;
	}

	batch_push(this, batch, pkt);

	flow->pkt = NULL;
}

static int lro_evict_flow(struct module *this, struct pkt_batch *batch,
		      struct lro_flow *flows)
{
	int oldest = 0;
	int i;

	/* We assume no slots are empty */
	for (i = 1; i < MAX_LRO_FLOWS; i++) {
		if (flows[oldest].usec > flows[i].usec)
			oldest = i;
	}

	lro_flush_flow(this, batch, &flows[oldest]);
	return oldest;
}

static void lro_init_flow(struct lro_flow *flow, struct snbuf *pkt,
		      uint16_t ip_offset, uint16_t tcp_offset,
		      flow_type_t type)
{
	struct ipv4_hdr *ip = (struct ipv4_hdr *)(snb_head_data(pkt) + ip_offset);
	struct tcp_hdr *tcp = (struct tcp_hdr *)(snb_head_data(pkt) + tcp_offset);

	uint32_t payload_offset = tcp_offset + (tcp->data_off >> 4) * 4;
	uint32_t payload_size = snb_total_len(pkt) - payload_offset;

	flow->pkt = pkt;
	flow->last_mbuf = &pkt->mbuf;
	flow->usec = current_us;

	flow->src_addr = ip->src_addr;
	flow->dst_addr = ip->dst_addr;
	flow->src_port = tcp->src_port;
	flow->dst_port = tcp->dst_port;

	flow->next_seq = rte_be_to_cpu_32(tcp->sent_seq) + payload_size;

	flow->ip_offset = ip_offset;
	flow->tcp_offset = tcp_offset;
	flow->type = type;
}

static void lro_append_pkt(struct module *this, struct pkt_batch *batch,
		       struct lro_flow *flow, struct snbuf *pkt,
		       uint16_t ip_offset, uint16_t tcp_offset,
		       flow_type_t type)
{
	struct tcp_hdr *tcp = (struct tcp_hdr *)(snb_head_data(pkt) + tcp_offset);

	uint32_t new_seq = rte_be_to_cpu_32(tcp->sent_seq);

	uint32_t payload_offset = tcp_offset + (tcp->data_off >> 4) * 4;
	uint32_t payload_size = snb_total_len(pkt) - payload_offset;
	
	struct tcp_hdr *old_tcp = (struct tcp_hdr *)
			(snb_head_data(flow->pkt) + flow->tcp_offset);

	assert(snb_is_linear(pkt));

	if (flow->next_seq != new_seq || old_tcp->recv_ack != tcp->recv_ack) {
		lro_flush_flow(this, batch, flow);
		batch_push(this, batch, pkt);
		return;
	}

	if (snb_total_len(flow->pkt) + payload_size > MAX_LFRAME) {
		lro_flush_flow(this, batch, flow);
		lro_init_flow(flow, pkt, ip_offset, tcp_offset, type);
		return;
	}

	flow->pkt->rx.gso_mss = RTE_MAX(flow->pkt->rx.gso_mss, payload_size);
	old_tcp->tcp_flags |= tcp->tcp_flags;

	snb_adj(pkt, payload_offset);

	/* attach */
	assert(snb_is_simple(pkt));
	flow->pkt->mbuf.pkt_len += snb_total_len(pkt);
	flow->pkt->mbuf.nb_segs++;
	flow->last_mbuf->next = &pkt->mbuf;
	flow->last_mbuf = &pkt->mbuf;

	/* if TCP flags other than ACK are on, flush */
	if (old_tcp->tcp_flags & 0xef) {
		lro_flush_flow(this, batch, flow);
		return;
	}

	flow->next_seq = new_seq + payload_size;
}

static void do_lro(struct module *this, struct pkt_batch *batch,
		   struct snbuf *pkt)
{
	struct lro_flow *flows = get_priv_worker(this, struct lro_flow *);
	struct ipv4_hdr *ip;
	struct tcp_hdr *tcp;
	
	uint16_t ip_offset;
	uint16_t tcp_offset;
	flow_type_t type;

	int free_slot = -1;
	int i;

	/* perform LRO only for packets from physical interfaces */
	if (port_map[pkt->in_port]->port_type != port_type_pport) {
		batch_push(this, batch, pkt);
		return;
	}

	if (pkt->rx.csum_state != SN_RX_CSUM_CORRECT) {
		batch_push(this, batch, pkt);
		return;
	}

	type = check_type(pkt, &ip_offset, &tcp_offset);
	if (type == FLOW_UNSUPPORTED) {
		batch_push(this, batch, pkt);
		return;
	}

	ip = (struct ipv4_hdr *)(snb_head_data(pkt) + ip_offset);
	tcp = (struct tcp_hdr *)(snb_head_data(pkt) + tcp_offset);

	for (i = 0; i < MAX_LRO_FLOWS; i++) {
		if (!flows[i].pkt) {
			if (free_slot == -1)
				free_slot = i;
			continue;
		}

		if (flows[i].src_addr == ip->src_addr &&
		    flows[i].dst_addr == ip->dst_addr &&
		    flows[i].src_port == tcp->src_port &&
		    flows[i].dst_port == tcp->dst_port)
		{
			lro_append_pkt(this, batch, &flows[i], pkt, 
					ip_offset, tcp_offset, type);
			return;
		}
	}

	/* Here, there is no existing flow for the TCP packet. */

	/* Should we buffer this packet? */
	if (tcp->tcp_flags & 0xef) {
		/* Bypass if there are any flags other than ack */
		batch_push(this, batch, pkt);
		return;
	}

	if (free_slot == -1)
		free_slot = lro_evict_flow(this, batch, flows);

	lro_init_flow(&flows[free_slot], pkt, ip_offset, tcp_offset, type);
}

static void lro_op_init_worker(struct module *this)
{
	struct lro_flow *flows;
	flows = rte_zmalloc(NULL, sizeof(struct lro_flow) * MAX_LRO_FLOWS, 0);

	set_priv_worker(this, flows);

	/* in microseconds */
	reset_timer_periodic(this, 50);
}

static void lro_op_process_batch(struct module *this,
				 struct pkt_batch *batch)
{
#if 0
	run_next_module(this, batch);
#else
	struct pkt_batch new_batch;
	int i;

	batch_clear(&new_batch);

	for (i = 0; i < batch->cnt; i++) {
		struct snbuf *pkt = batch->pkts[i];

		do_lro(this, &new_batch, pkt);
	}

	run_next_module(this, &new_batch);
#endif
}

static int lro_op_timer(struct module *this)
{
	struct lro_flow *flows = get_priv_worker(this, struct lro_flow *);
	struct pkt_batch batch;
	uint64_t now = current_us;
	int ret = 0;
	int i;

	batch_clear(&batch);

	for (i = 0; i < MAX_LRO_FLOWS; i++) {
		if (!flows[i].pkt)
			continue;

		/* If older than 100us, flush.
		 * While 100us seems too much, it is not.
		 * (we immediately flush packets if PSH is seen) */
		if (now - flows[i].usec > 100) {
			lro_flush_flow(this, &batch, &flows[i]);
			ret++;
		}
	}

	run_next_module(this, &batch);

	return ret;
}

const struct module_ops lro_ops = {
	.init_worker 	= lro_op_init_worker,
	.process_batch	= lro_op_process_batch,
	.timer 		= lro_op_timer,
};
