// Copyright (c) 2014-2016, The Regents of the University of California.
// Copyright (c) 2016-2017, Nefeli Networks, Inc.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// * Redistributions of source code must retain the above copyright notice,
// this list of conditions and the following disclaimer.
//
// * Redistributions in binary form must reproduce the above copyright notice,
// this list of conditions and the following disclaimer in the documentation
// and/or other materials provided with the distribution.
//
// * Neither the names of the copyright holders nor the names of their
// contributors may be used to endorse or promote products derived from this
// software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.

#include <linux/version.h>
#include <linux/etherdevice.h>
#include <linux/if_vlan.h>
#include <linux/timex.h>

/* Loom: For queue assignment hack. */
#include <net/sock.h>

#ifndef UTS_RELEASE
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,33)
#include <linux/utsrelease.h>
#else
#include <generated/utsrelease.h>
#endif
#endif

/* disable for now, since it is not tested with new vport implementation */
#undef CONFIG_NET_RX_BUSY_POLL

#ifdef CONFIG_NET_RX_BUSY_POLL
#include <net/busy_poll.h>
#endif

#ifndef NAPI_POLL_WEIGHT
#define NAPI_POLL_WEIGHT 64
#endif

#include "sn_common.h"
#include "sn_kernel.h"
#include "../snbuf_layout.h"

static int sn_poll(struct napi_struct *napi, int budget);
static int sn_poll_tx(struct napi_struct *napi, int budget);
static void sn_enable_interrupt(struct sn_queue *rx_queue);
static void sn_enable_interrupt_tx(struct sn_queue *tx_queue);

static void sn_test_cache_alignment(struct sn_device *dev)
{
	int i;

	for (i = 0; i < dev->num_tx_ctrlq; i++) {
		struct sn_queue *q = dev->tx_ctrl_queues[i];

		if ((((uintptr_t) q->drv_to_sn) % L1_CACHE_BYTES) ||
		    (((uintptr_t) q->sn_to_drv) % L1_CACHE_BYTES))
		{
			pr_err("invalid cache alignment: %p %p\n",
					q->drv_to_sn, q->sn_to_drv);
		}
	}

	for (i = 0; i < dev->num_rxq; i++) {
		struct sn_queue *q = dev->rx_queues[i];

		if ((((uintptr_t) q->drv_to_sn) % L1_CACHE_BYTES) ||
		    (((uintptr_t) q->sn_to_drv) % L1_CACHE_BYTES) ||
		    (((uintptr_t) q->rx.rx_regs) % L1_CACHE_BYTES))
		{
			pr_err("invalid cache alignment: %p %p %p\n",
					q->drv_to_sn,
					q->sn_to_drv,
					q->rx.rx_regs);
		}
	}
}

static int sn_alloc_queues(struct sn_device *dev,
		void *rings, uint64_t rings_size,
		struct tx_queue_opts *txq_opts,
		struct rx_queue_opts *rxq_opts)
{
	struct sn_queue *queue;
	char *p = rings;

	void *memchunk;

	int num_queues;
	int i;

	int ret;

	ret = netif_set_real_num_tx_queues(dev->netdev, dev->num_tx_ctrlq);
	if (ret) {
		log_err("netif_set_real_num_tx_queues() failed\n");
		return ret;
	}

	ret = netif_set_real_num_rx_queues(dev->netdev, dev->num_rxq);
	if (ret) {
		log_err("netif_set_real_num_rx_queues() failed\n");
		return ret;
	}

	num_queues = dev->num_tx_ctrlq + dev->num_rxq + dev->num_tx_dataq;

	memchunk = kzalloc(sizeof(struct sn_queue) * num_queues, GFP_KERNEL);
	if (!memchunk)
		return -ENOMEM;

	queue = memchunk;

	for (i = 0; i < dev->num_tx_ctrlq; i++) {
		dev->tx_ctrl_queues[i] = queue;

		queue->dev = dev;
		queue->queue_id = i;
		queue->tx_ctrl.opts = *txq_opts;

		queue->tx_ctrl.netdev_txq = netdev_get_tx_queue(dev->netdev, i);

		queue->tx_ctrl.tx_regs = (struct sn_tx_ctrlq_registers *)p;
		p += sizeof(struct sn_tx_ctrlq_registers);

		queue->drv_to_sn = (struct llring *)p;
		p += llring_bytes(queue->drv_to_sn);

		queue->sn_to_drv = (struct llring *)p;
		p += llring_bytes(queue->sn_to_drv);

		queue++;
	}

	for (i = 0; i < dev->num_rxq; i++) {
		dev->rx_queues[i] = queue;

		queue->dev = dev;
		queue->queue_id = i;
		queue->rx.opts = *rxq_opts;

		queue->rx.rx_regs = (struct sn_rxq_registers *)p;
		p += sizeof(struct sn_rxq_registers);

		queue->drv_to_sn = (struct llring *)p;
		p += llring_bytes(queue->drv_to_sn);

		queue->sn_to_drv = (struct llring *)p;
		p += llring_bytes(queue->sn_to_drv);

		queue++;
	}

	for (i = 0; i < dev->num_tx_dataq; i++) {
		dev->tx_data_queues[i] = queue;

		queue->dev = dev;
		/* Loom: should data queues have some offset for queue ids? */
		queue->queue_id = i;
		queue->tx_data.opts = *txq_opts;

		queue->drv_to_sn = (struct llring *)p;
		p += llring_bytes(queue->drv_to_sn);

		queue++;
	}

	if ((uintptr_t)p != (uintptr_t)rings + rings_size) {
		log_err("Invalid ring space size: %llu, not %llu, at%p)\n",
				rings_size,
				(uint64_t)((uintptr_t)p - (uintptr_t)rings),
				rings);
		kfree(memchunk);
		return -EFAULT;
	}

	for (i = 0; i < dev->num_rxq; i++) {
		netif_napi_add(dev->netdev, &dev->rx_queues[i]->rx.napi,
				sn_poll, NAPI_POLL_WEIGHT);
#ifdef CONFIG_NET_RX_BUSY_POLL
		napi_hash_add(&dev->rx_queues[i]->rx.napi);
#endif
		spin_lock_init(&dev->rx_queues[i]->rx.lock);
	}

	for (i = 0; i < dev->num_tx_ctrlq; i++) {
		netif_napi_add(dev->netdev, &dev->tx_ctrl_queues[i]->tx_ctrl.napi,
				sn_poll_tx, NAPI_POLL_WEIGHT);
	}

	sn_test_cache_alignment(dev);

	return 0;
}

static void sn_free_queues(struct sn_device *dev)
{
	int i;

	for (i = 0; i < dev->num_rxq; i++) {
#ifdef CONFIG_NET_RX_BUSY_POLL
		napi_hash_del(&dev->rx_queues[i]->rx.napi);
#endif
		netif_napi_del(&dev->rx_queues[i]->rx.napi);
	}


	for (i = 0; i < dev->num_tx_ctrlq; i++) {
		netif_napi_del(&dev->tx_ctrl_queues[i]->tx_ctrl.napi);
	}

	/* Queues are allocated in batch,
	 * and the tx_queues[0] is its address */
	kfree(dev->tx_ctrl_queues[0]);
}

/* Interface up */
static int sn_open(struct net_device *netdev)
{
	struct sn_device *dev = netdev_priv(netdev);
	int i;

	for (i = 0; i < dev->num_rxq; i++)
		napi_enable(&dev->rx_queues[i]->rx.napi);
	for (i = 0; i < dev->num_rxq; i++)
		sn_enable_interrupt(dev->rx_queues[i]);

	for (i = 0; i < dev->num_tx_ctrlq; i++)
		napi_enable(&dev->tx_ctrl_queues[i]->tx_ctrl.napi);
	for (i = 0; i < dev->num_tx_ctrlq; i++)
		sn_enable_interrupt_tx(dev->tx_ctrl_queues[i]);

	return 0;
}

/* Interface down, but the device itself is still alive */
static int sn_close(struct net_device *netdev)
{
	struct sn_device *dev = netdev_priv(netdev);
	int i;

	for (i = 0; i < dev->num_rxq; i++)
		napi_disable(&dev->rx_queues[i]->rx.napi);

	for (i = 0; i < dev->num_tx_ctrlq; i++)
		napi_disable(&dev->tx_ctrl_queues[i]->tx_ctrl.napi);

	return 0;
}

static void sn_enable_interrupt(struct sn_queue *rx_queue)
{
	__sync_synchronize();
	rx_queue->rx.rx_regs->irq_disabled = 0;
	__sync_synchronize();

	/* NOTE: make sure check again if the queue is really empty,
	 * to avoid potential race conditions when you call this function:
	 *
	 * Driver:			BESS:
	 * [IRQ is disabled]
	 * [doing polling]
	 * if (no pending packet)
	 * 				push a packet
	 * 				if (IRQ enabled)
	 * 					inject IRQ <- not executed
	 *     stop polling
	 *     enable IRQ
	 *
	 * [at this point, IRQ is enabled but pending packets are never
	 *  polled by the driver. So the driver needs to double check.]
	 */
}

static void sn_enable_interrupt_tx(struct sn_queue *tx_queue)
{
	/* LOOM: DEBUG */
	//log_info("%s: sn_enable_interrupt_tx tx_queue=%d\n",
	//	 tx_queue->dev->netdev->name, tx_queue->queue_id);
	//trace_printk("%s: sn_enable_interrupt_tx on tx_queue %d\n",
	//	 tx_queue->dev->netdev->name, tx_queue->queue_id);

	/* LOOM: DEBUG: TODO: Less extreme synchronization */
	//smp_mb();
	__sync_synchronize();
	tx_queue->tx_ctrl.tx_regs->irq_disabled = 0;
	__sync_synchronize();
	//smp_mb();

	/* LOOM: DEBUG */
	//trace_printk("%s: sn_enable_interrupt_tx on tx_queue %d. "
	//	 "irq_disabled: %d\n", tx_queue->dev->netdev->name,
	//	 tx_queue->queue_id, tx_queue->tx.tx_regs->irq_disabled);

	/* NOTE: make sure check again if the queue is really empty,
	 * to avoid potential race conditions when you call this function:
	 *
	 * Driver:			BESS:
	 * [IRQ is disabled]
	 * [doing polling]
	 * if (no pending packet)
	 * 				push a packet
	 * 				if (IRQ enabled)
	 * 					inject IRQ <- not executed
	 *     stop polling
	 *     enable IRQ
	 *
	 * [at this point, IRQ is enabled but pending packets are never
	 *  polled by the driver. So the driver needs to double check.]
	 */
}

static void sn_disable_interrupt(struct sn_queue *rx_queue)
{
	/* the interrupt is usually disabled by BESS,
	 * but in some cases the driver itself may also want to disable IRQ
	 * (e.g., for low latency socket polling) */

	rx_queue->rx.rx_regs->irq_disabled = 1;
}

static void sn_disable_interrupt_tx(struct sn_queue *tx_queue)
{
	//trace_printk("%s: sn_disable_interrupt_tx on tx_queue %d\n",
	//	 tx_queue->dev->netdev->name, tx_queue->queue_id);

	tx_queue->tx_ctrl.tx_regs->irq_disabled = 1;
}

static void sn_process_rx_metadata(struct sk_buff *skb,
				   struct sn_rx_metadata *rx_meta)
{
	if (rx_meta->gso_mss) {
		skb_shinfo(skb)->gso_size = rx_meta->gso_mss;
		skb_shinfo(skb)->gso_type = SKB_GSO_TCPV4;
	}

	/* By default, skb->ip_summed == CHECKSUM_NONE */
	skb_checksum_none_assert(skb);

	switch (rx_meta->csum_state) {
	case SN_RX_CSUM_CORRECT_ENCAP:
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,13,0)
		/* without this the upper layer won't respect skb->ip_summed */
		skb->encapsulation = 1;
#endif
		/* fall through */

	case SN_RX_CSUM_CORRECT:
		skb->ip_summed = CHECKSUM_UNNECESSARY;
		break;

	case SN_RX_CSUM_INCORRECT:
		/* Incorrect L4/IP checksum */
		/* fall through, so that packets can be still visible */

	default:
		; /* do nothing */
	}
}

static inline netdev_tx_t sn_send_tx_queue(struct sn_queue *ctrl_queue,
					   struct sn_queue* data_queue,
					   struct sn_device* dev,
					   struct sk_buff* skb);

DEFINE_PER_CPU(int, in_batched_polling);

static void sn_process_loopback(struct sn_device *dev,
		struct sk_buff *skbs[], int cnt)
{
	struct sn_queue *tx_ctrl_queue;
	struct sn_queue *tx_data_queue;

	int ctrl_qid;
	int data_qid;
	int cpu;
	int i;

	int lock_required;

	cpu = raw_smp_processor_id();
	ctrl_qid = dev->cpu_to_tx_ctrlq[cpu];
	data_qid = 0; /* Loom: Assume loopback is infrequent. Revisit later. */
	tx_ctrl_queue = dev->tx_ctrl_queues[ctrl_qid];
	tx_data_queue = dev->tx_data_queues[data_qid];

	lock_required = (tx_ctrl_queue->tx_ctrl.netdev_txq->xmit_lock_owner != cpu);

	if (lock_required)
		HARD_TX_LOCK(dev->netdev, tx_ctrl_queue->tx_ctrl.netdev_txq, cpu);

	for (i = 0; i < cnt; i++) {
		if (!skbs[i])
			continue;

		/* Ignoring return value here */
		sn_send_tx_queue(tx_ctrl_queue, tx_data_queue, dev, skbs[i]);
	}

	if (lock_required)
		HARD_TX_UNLOCK(dev->netdev, tx_ctrl_queue->tx_ctrl.netdev_txq);
}

static int sn_poll_action_batch(struct sn_queue *rx_queue, int budget)
{
	struct napi_struct *napi = &rx_queue->rx.napi;
	struct sn_device *dev = rx_queue->dev;

	int poll_cnt = 0;

	int *polling;

	polling = this_cpu_ptr(&in_batched_polling);
	*polling = 1;

	while (poll_cnt < budget) {
		struct sk_buff *skbs[MAX_BATCH];
		struct sn_rx_metadata rx_meta[MAX_BATCH];

		int cnt;
		int i;

		cnt = dev->ops->do_rx_batch(rx_queue, rx_meta, skbs,
				min(MAX_BATCH, budget - poll_cnt));
		if (cnt == 0)
			break;

		rx_queue->rx.stats.packets += cnt;
		poll_cnt += cnt;

		for (i = 0; i < cnt; i++) {
			struct sk_buff *skb = skbs[i];
			int ret;

			if (unlikely(!skb))
				continue;

			rx_queue->rx.stats.bytes += skb->len;

			sn_process_rx_metadata(skb, &rx_meta[i]);
		}

		if (!rx_queue->rx.opts.loopback) {
			for (i = 0; i < cnt; i++) {
				struct sk_buff *skb = skbs[i];
				if (!skb)
					continue;

				skb_record_rx_queue(skb, rx_queue->queue_id);
				skb->protocol = eth_type_trans(skb, napi->dev);
#ifdef CONFIG_NET_RX_BUSY_POLL
				skb_mark_napi_id(skb, napi);
#endif
				netif_receive_skb(skb);
			}
		} else
			sn_process_loopback(dev, skbs, cnt);
	}

	if (dev->ops->flush_tx)
		dev->ops->flush_tx();

	*polling = 0;

	return poll_cnt;
}

static int sn_poll_action_single(struct sn_queue *rx_queue, int budget)
{
	struct napi_struct *napi = &rx_queue->rx.napi;
	int poll_cnt = 0;

	while (poll_cnt < budget) {
		struct sk_buff *skb;
		struct sn_rx_metadata rx_meta;
		int ret;

		skb = rx_queue->dev->ops->do_rx(rx_queue, &rx_meta);
		if (!skb)
			return poll_cnt;

		rx_queue->rx.stats.packets++;
		rx_queue->rx.stats.bytes += skb->len;

		sn_process_rx_metadata(skb, &rx_meta);

		skb_record_rx_queue(skb, rx_queue->queue_id);
		skb->protocol = eth_type_trans(skb, napi->dev);
#ifdef CONFIG_NET_RX_BUSY_POLL
		skb_mark_napi_id(skb, napi);
#endif

		netif_receive_skb(skb);

		poll_cnt++;
	}

	return poll_cnt;
}

static int sn_poll_action(struct sn_queue *rx_queue, int budget)
{
	if (rx_queue->dev->ops->do_rx_batch)
		return sn_poll_action_batch(rx_queue, budget);
	else
		return sn_poll_action_single(rx_queue, budget);
}

#ifdef CONFIG_NET_RX_BUSY_POLL
#define SN_BUSY_POLL_BUDGET	4
/* Low latency socket callback. Called with bh disabled */
static int sn_poll_ll(struct napi_struct *napi)
{
	struct sn_queue *rx_queue;

	int idle_cnt = 0;
	int ret;

	rx_queue = container_of(napi, struct sn_queue, rx.napi);

	if (!spin_trylock(&rx_queue->lock))
		return LL_FLUSH_BUSY;

	rx_queue->rx.stats.ll_polls++;

	sn_disable_interrupt(rx_queue);

	/* Meh... Since there is no notification for busy loop completion,
	 * there is no clean way to avoid race condition w.r.t. interrupts.
	 * Instead, do a roughly 5-us polling in this function. */

	do {
		ret = sn_poll_action(rx_queue, SN_BUSY_POLL_BUDGET);
		if (ret == 0)
			cpu_relax();
	} while (ret == 0 && idle_cnt++ < 1000);

	sn_enable_interrupt(rx_queue);

	if (rx_queue->dev->ops->pending_rx(rx_queue)) {
		sn_disable_interrupt(rx_queue);
		napi_schedule(napi);
	}

	spin_unlock(&rx_queue->lock);

	return ret;
}
#endif

/* NAPI callback */
/* The return value says how many packets are actually received */
static int sn_poll(struct napi_struct *napi, int budget)
{
	struct sn_queue *rx_queue;

	int ret;

	rx_queue = container_of(napi, struct sn_queue, rx.napi);

	if (!spin_trylock(&rx_queue->rx.lock))
		return 0;

	rx_queue->rx.stats.polls++;

	ret = sn_poll_action(rx_queue, budget);

	if (ret < budget) {
		napi_complete(napi);
		sn_enable_interrupt(rx_queue);

		/* last check for race condition.
		 * see sn_enable_interrupt() */
		if (rx_queue->dev->ops->pending_rx(rx_queue)) {
			napi_reschedule(napi);
			sn_disable_interrupt(rx_queue);
		}
	}

	spin_unlock(&rx_queue->rx.lock);

	return ret;
}

int sn_maybe_stop_tx(struct sn_queue *queue)
{
	struct net_device *netdev = queue->dev->netdev;

	int limit = 0;
	//int limit = (MAX_BATCH);

	/* LOOM: DEBUG */
	//log_info("%s: sn_maybe_stop_tx. avail_snbs=%d, tx_queue=%d\n",
	//	 queue->dev->netdev->name, sn_avail_snbs(queue),
	//	 queue->queue_id);

	/* TODO: assert that queue is a tx_queue */

	if (sn_avail_snbs(queue) > limit &&
	    sn_avail_ctrl_desc(queue) > limit) {
		return 0;
	}


	/* LOOM: DEBUG */
	//log_info("%s: sn_maybe_stop_tx stopping tx_queue=%d\n",
	//	 queue->dev->netdev->name, queue->queue_id);
	//trace_printk("%s: sn_maybe_stop_tx stopping tx_queue=%d\n",
	//	 queue->dev->netdev->name, queue->queue_id);
	/* Loom: DEBUG: TODO trace_printk the reason the queue is being stopped. */

	netif_stop_subqueue(netdev, queue->queue_id);
	sn_enable_interrupt_tx(queue);

	queue->tx_ctrl.stats.stop_queue++;

	/* TODO: Which sync function? */
	smp_mb();
	//__sync_synchronize();

	if (likely(sn_avail_snbs(queue) <= limit))
		return -EBUSY;

	/* LOOM: DEBUG */
	//trace_printk("%s: sn_maybe_stop_tx reprieve for tx_queue=%d\n",
	//	 queue->dev->netdev->name, queue->queue_id);

	/* A reprieve! (race condition. ixgbe inspired) */
	/* Why is this netif_start_subqueue in ixgbe instead of
	 * netif_wake_subqueue? */
	netif_start_subqueue(netdev, queue->queue_id);
	/* TODO: Disable the interrupt again? */
	sn_disable_interrupt_tx(queue);

	queue->tx_ctrl.stats.restart_queue++;

	return 0;
}

/* NAPI callback */
/* The return value says how many packets are actually received */
static int sn_poll_tx(struct napi_struct *napi, int budget)
{
	struct sn_queue *tx_queue;

	int ret;

	tx_queue = container_of(napi, struct sn_queue, tx_ctrl.napi);

	tx_queue->tx_ctrl.stats.polls++;

	/* LOOM: DEBUG */
	//log_info("%s: sn_poll_tx on tx_queue %d\n",
	//	 tx_queue->dev->netdev->name, tx_queue->queue_id);
	//trace_printk("%s: sn_poll_tx on tx_queue %d\n",
	//	 tx_queue->dev->netdev->name, tx_queue->queue_id);

	/* This is supposed to be the number of buffers that have been
	 * reclaimed.  Returning 0 should be fine for now given that we also
	 * call napi_complete. */
	ret = 0;

        /* TODO: check the number of available snb's and restart TX if it was
         * stopped. */
	if (__netif_subqueue_stopped(tx_queue->dev->netdev, tx_queue->queue_id) &&
	    sn_avail_snbs(tx_queue) > 0) {
		netif_wake_subqueue(tx_queue->dev->netdev, tx_queue->queue_id);
		tx_queue->tx_ctrl.stats.restart_queue++;

		napi_complete(napi);

		/* LOOM: I think the interrupt should be disabled now until we
		 * are starved for snb's again in sn_maybe_stop_tx. */
		/* TODO: */
		sn_disable_interrupt_tx(tx_queue);

		/* LOOM: There is a race condition here where the queue could
		 * be empty again and we would like to wait for interrupts
		 * again. */
		sn_maybe_stop_tx(tx_queue);
	/* Why are we getting an interrupt if the queue is not stopped? */
	} else if (!__netif_subqueue_stopped(tx_queue->dev->netdev, tx_queue->queue_id)) {

		/* LOOM: DEBUG */
		//log_info("%s: sn_poll_tx on tx_queue %d that is not stopped!?\n",
		//	 tx_queue->dev->netdev->name, tx_queue->queue_id);

		napi_complete(napi);
		sn_disable_interrupt_tx(tx_queue);
		sn_maybe_stop_tx(tx_queue);
	/* Why did we get kicked if there are no available snbs?  Either way,
	 * we should finish NAPI, leave the interrupt enabled, and then wait
	 * until BESS kicks us again. */
	} else {
		napi_complete(napi);
		/* sn_maybe_stop_tx will enable the interrupt, so this is redundant. */
		sn_enable_interrupt_tx(tx_queue);
		sn_maybe_stop_tx(tx_queue);
	}

	/* LOOM: DEBUG */
	if (sn_avail_snbs(tx_queue) == 0 && unlikely(net_ratelimit())) {
		log_info("%s: sn_poll_tx on tx_queue %d. No avail snbs (%d)!\n",
			 tx_queue->dev->netdev->name, tx_queue->queue_id,
			 sn_avail_snbs(tx_queue));
	}

	return ret;
}

static void sn_set_tx_metadata(struct sk_buff *skb,
			       struct sn_tx_data_metadata *tx_meta)
{
	if (skb->ip_summed == CHECKSUM_PARTIAL) {
		tx_meta->csum_start = skb_checksum_start_offset(skb);
		tx_meta->csum_dest = tx_meta->csum_start + skb->csum_offset;

                /* LOOM: DEBUG: */
                /* TODO: ftrace instead? */
                //log_info("CHECKSUM_PARTIAL: setting tx_meta\n");
	} else  {
		tx_meta->csum_start = SN_TX_CSUM_DONT;
		tx_meta->csum_dest = SN_TX_CSUM_DONT;
	}

        //tx_meta->drv_xmit_ts = get_cycles();
}

static inline netdev_tx_t sn_send_tx_queue(struct sn_queue *ctrl_queue,
					   struct sn_queue* data_queue,
					   struct sn_device* dev,
					   struct sk_buff* skb)
{
	struct sn_tx_data_metadata tx_meta;
	int ret = NET_XMIT_DROP;

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,10,0)
	if (ctrl_queue->tx.opts.tci) {
		skb = vlan_insert_tag(skb, queue->tx.opts.tci);
		if (unlikely(!skb))
			goto skip_send;
	}
#else
	if (ctrl_queue->tx_ctrl.opts.tci) {
		skb = vlan_insert_tag(skb, htons(ETH_P_8021Q),
				ctrl_queue->tx_ctrl.opts.tci);
		if (unlikely(!skb))
			goto skip_send;
	}

	if (ctrl_queue->tx_ctrl.opts.outer_tci) {
		skb = vlan_insert_tag(skb, htons(ETH_P_8021AD),
				ctrl_queue->tx_ctrl.opts.outer_tci);
		if (unlikely(!skb))
			goto skip_send;
	}
#endif

	/* Set metadata before orphaning so that traffic class info can be
	 * saved from the socket if needed. */
	sn_set_tx_metadata(skb, &tx_meta);

        /* Loom: TODO: I don't feel like orphaning the skb is necessary here.
	 * In particular, I feel like this breaks TCP Small Queues (although so
	 * does tricking Linux into not using Qdisc). */
	/* Loom: Given the addition of NETDEV_TX_BUSY, this makes me concerned
	 * given that the skb may not always be freed. */
	skb_orphan(skb);

	/* Actually do the TX. */
	ret = dev->ops->do_tx(ctrl_queue, data_queue, skb, &tx_meta);

skip_send:
	switch (ret) {
	case NET_XMIT_CN:
		ctrl_queue->tx_ctrl.stats.throttled++;
		/* fall through */

	case NETDEV_TX_OK:
		ctrl_queue->tx_ctrl.stats.packets++;
		ctrl_queue->tx_ctrl.stats.bytes += skb->len;
		break;

	case NETDEV_TX_BUSY:
		sn_maybe_stop_tx(ctrl_queue);
		ctrl_queue->tx_ctrl.stats.busy++;
		/* should not free skb */
		return NETDEV_TX_BUSY;

	/* LOOM: This case is not possible anymore? */
	case NET_XMIT_DROP:
		/* LOOM: DEBUG. */
		log_info("%s: dropped packet tx_queue=%d\n",
			 ctrl_queue->dev->netdev->name, ctrl_queue->queue_id);

		ctrl_queue->tx_ctrl.stats.dropped++;
		break;

	case SN_NET_XMIT_BUFFERED:
		/* should not free skb */
		return NETDEV_TX_OK;
	}

	dev_kfree_skb(skb);
	return ret;
}

/* Loom: TODO: More sophisticated algorithm. */
static u32 sn_select_data_queue(struct sn_device *dev, struct sk_buff *skb)
{
	u32 sched_qi;

	atomic_t *nqm = &dev->qa_state.next_dataq;
	sched_qi = atomic_add_return(1, nqm) % 
		dev->num_tx_dataq;

	return sched_qi;
}

/* As a soft device without qdisc,
 * this function returns NET_XMIT_* instead of NETDEV_TX_* */
/* Loom: I want qdisc, so I changed the return value. */
static int sn_start_xmit(struct sk_buff *skb, struct net_device *netdev)
{
	struct sn_device *dev = netdev_priv(netdev);
	struct sn_queue *ctrl_queue;
	struct sn_queue *data_queue;

	u16 tx_ctrlq = skb->queue_mapping;
	u32 tx_dataq;

	if (dev->dataq_on) {
		if (skb->sk && skb->sk->sk_tx_sched_queue_mapping >= 0) {
			tx_dataq = skb->sk->sk_tx_sched_queue_mapping;
		} else {
			/* An skb could be assigned a scheduling queue in
			 * netdev_pick_tx. */
			tx_dataq = sn_select_data_queue(dev, skb);
		}
	}

	/* log_info("tx_ctrlq=%d cpu=%d\n", txq, raw_smp_processor_id()); */

	if (unlikely(skb->len > SNBUF_DATA)) {
		log_err("too large skb! (%d)\n", skb->len);
		dev_kfree_skb(skb);
		return NET_XMIT_DROP;
	}

	if (unlikely(skb_shinfo(skb)->frag_list)) {
		log_err("frag_list is not NULL!\n");
		dev_kfree_skb(skb);
		return NET_XMIT_DROP;
	}

	if (unlikely(tx_ctrlq >= dev->num_tx_ctrlq)) {
		log_err("invalid tx_ctrlq=%u\n", tx_ctrlq);
		dev_kfree_skb(skb);
		return NET_XMIT_DROP;
	}

	if (unlikely(dev->dataq_on && tx_dataq >= dev->num_tx_dataq)) {
		log_err("invalid tx_dataq=%u\n", tx_dataq);
		dev_kfree_skb(skb);
		return NET_XMIT_DROP;
	}

	ctrl_queue = dev->tx_ctrl_queues[tx_ctrlq];
	data_queue = (dev->dataq_on) ? dev->tx_data_queues[tx_dataq] : NULL;
	return sn_send_tx_queue(ctrl_queue, data_queue, dev, skb);
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,13,0)
static u16 sn_select_queue(struct net_device *netdev, struct sk_buff *skb)
#elif LINUX_VERSION_CODE < KERNEL_VERSION(3,14,0) && \
      (!defined(UTS_UBUNTU_RELEASE_ABI) || UTS_UBUNTU_RELEASE_ABI < 24)
static u16 sn_select_queue(struct net_device *netdev,
			   struct sk_buff *skb,
			   void *accel_priv)
#else
static u16 sn_select_queue(struct net_device *netdev,
			   struct sk_buff *skb,
			   void *accel_priv,
			   select_queue_fallback_t fallback)
#endif
{
	struct sn_device *dev = netdev_priv(netdev);
	struct sock *sk = skb->sk;

	/* Loom: Note/Hack: this function is used to also pick the
	 * data/scheduling queue used.
	 * - This could be done in a local hash table to avoid using a modified
	 *   kernel.
	 * - A more intelligent queue selection algorithm will probably be
	 *   appropriate.
	 *   - The current implementation does not avoid oversubscribing
	 *     queues.
	 *   - Per-CPU scheduling queues?
	 *   - Use a bitmask?
	 */
	if (sk) {
		/* This could be a separate function call (ndo) from
		 * netdev_pick_tx */
		int sched_qi = sk->sk_tx_sched_queue_mapping;
		if (sched_qi < 0) {
			sched_qi = sn_select_data_queue(dev, skb);
			sk->sk_tx_sched_queue_mapping = sched_qi;

			trace_printk("Set sched queue for sk (%p) to %d\n",
				sk, sk->sk_tx_sched_queue_mapping);
		}
	}

	return dev->cpu_to_tx_ctrlq[raw_smp_processor_id()];
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(4,11,0)
static
struct rtnl_link_stats64 *sn_get_stats64(struct net_device *netdev,
          struct rtnl_link_stats64 *storage)
#else
static void sn_get_stats64(struct net_device *netdev,
          struct rtnl_link_stats64 *storage)
#endif
{
	struct sn_device *dev = netdev_priv(netdev);

	int i;

	for (i = 0; i < dev->num_tx_ctrlq; i++) {
		storage->tx_packets 	+= dev->tx_ctrl_queues[i]->tx_ctrl.stats.packets;
		storage->tx_bytes 	+= dev->tx_ctrl_queues[i]->tx_ctrl.stats.bytes;
		storage->tx_dropped 	+= dev->tx_ctrl_queues[i]->tx_ctrl.stats.dropped;
	}

	for (i = 0; i < dev->num_rxq; i++) {
		dev->rx_queues[i]->rx.stats.dropped =
				dev->rx_queues[i]->rx.rx_regs->dropped;

		storage->rx_packets 	+= dev->rx_queues[i]->rx.stats.packets;
		storage->rx_bytes 	+= dev->rx_queues[i]->rx.stats.bytes;
		storage->rx_dropped 	+= dev->rx_queues[i]->rx.stats.dropped;
	}
#if LINUX_VERSION_CODE < KERNEL_VERSION(4,11,0)
  return storage;
#endif
}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(3,3,0))
typedef uint32_t netdev_features_t;
#endif

netdev_features_t sn_fix_features(struct net_device *dev,
				  netdev_features_t features)
{
	return features & ~NETIF_F_NOCACHE_COPY;
}

static const struct net_device_ops sn_netdev_ops = {
	.ndo_open		= sn_open,
	.ndo_stop		= sn_close,
#ifdef CONFIG_NET_RX_BUSY_POLL
	.ndo_busy_poll		= sn_poll_ll,
#endif
	.ndo_start_xmit		= sn_start_xmit,
	.ndo_select_queue	= sn_select_queue,
	.ndo_get_stats64 	= sn_get_stats64,
	.ndo_fix_features	= sn_fix_features,
	.ndo_set_mac_address    = eth_mac_addr,
	.ndo_validate_addr      = eth_validate_addr,
};

extern const struct ethtool_ops sn_ethtool_ops;

static void sn_set_offloads(struct net_device *netdev)
{
#ifdef LOOM
	/* LOOM: DEBUG */
	//netif_set_gso_max_size(netdev, SNBUF_DATA);
	netif_set_gso_max_size(netdev, 65536);
	//netif_set_gso_max_size(netdev, 16384);

	//netdev->hw_features = NETIF_F_SG |
	//		      NETIF_F_IP_CSUM |
	//		      NETIF_F_TSO |
	//		      NETIF_F_TSO_ECN;
	//netdev->hw_features = NETIF_F_SG |
	//		      NETIF_F_IP_CSUM;

        //netdev->hw_features = 0;

	netdev->hw_features = NETIF_F_SG |
			      NETIF_F_IP_CSUM |
			      //NETIF_F_HW_CSUM |
			      NETIF_F_RXCSUM |
			      NETIF_F_FRAGLIST |
			      NETIF_F_LRO |
			      //NETIF_F_GRO |
			      NETIF_F_GSO;
#elif 0
	netdev->hw_features = NETIF_F_SG |
			      NETIF_F_IP_CSUM |
			      NETIF_F_RXCSUM |
			      NETIF_F_TSO |
			      NETIF_F_TSO_ECN |
			      NETIF_F_LRO |
			      NETIF_F_GSO_UDP_TUNNEL;
#else
	/* Disable all offloading features for now */
	netdev->hw_features = 0;
#endif

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,8,0))
	netdev->hw_enc_features = netdev->hw_features;
#endif

	/* We prevent this interface from moving around namespaces.
	 * This is to work around race between device unregister and namespace
	 * cleanup. Revise this after adopting rtnl link based design */
	netdev->features = netdev->hw_features | NETIF_F_NETNS_LOCAL;
}

static void sn_set_default_queue_mapping(struct sn_device *dev)
{
	int cpu;
	int rxq;

	for_each_possible_cpu(cpu) {
		dev->cpu_to_tx_ctrlq[cpu] = 0;
		dev->cpu_to_rxqs[cpu][0] = -1;
	}

	for_each_online_cpu(cpu) {
		dev->cpu_to_tx_ctrlq[cpu] = cpu % dev->num_tx_ctrlq;
	}

	for (rxq = 0; rxq < dev->num_rxq; rxq++) {
		for_each_online_cpu(cpu) {
			int cnt;
			for (cnt = 0; dev->cpu_to_rxqs[cpu][cnt] != -1; cnt++)
				/* do nothing */ ;

			dev->cpu_to_rxqs[cpu][cnt] = rxq;
			dev->cpu_to_rxqs[cpu][cnt + 1] = -1;

			rxq++;
			if (rxq >= dev->num_rxq)
				break;
		}
	}
}

/* unregister_netdev(ice) will eventually trigger this function */
static void sn_netdev_destructor(struct net_device *netdev)
{
	struct sn_device *dev = netdev_priv(netdev);
	sn_free_queues(dev);
	log_info("%s: releasing netdev...\n", netdev->name);
	free_netdev(netdev);
}

/* bar must be a virtual address that kernel has direct access */
int sn_create_netdev(void *bar, struct sn_device **dev_ret)
{
	struct sn_conf_space *conf = bar;
	struct sn_device *dev;
	struct net_device *netdev;

	char *name;

	int ret;

	*dev_ret = NULL;

	if (conf->bar_size < sizeof(struct sn_conf_space)) {
		log_err("invalid BAR size %llu\n", conf->bar_size);
		return -EINVAL;
	}

	if (conf->num_tx_ctrlq < 1 || 
			(conf->num_tx_dataq < 1 && conf->dataq_on) ||
			conf->num_rxq < 1 ||
			conf->num_tx_ctrlq > MAX_QUEUES ||
			conf->num_tx_dataq > SN_MAX_TX_DATAQ ||
			conf->num_rxq > MAX_QUEUES)
	{
		log_err("invalid ioctl arguments: num_tx_ctrlq=%d, "
				"num_tx_dataq=%d, num_rxq=%d, dataq_on:%d\n",
				conf->num_tx_ctrlq, conf->num_tx_dataq,
				conf->num_rxq, conf->dataq_on);
		return -EINVAL;
	}
	log_err("ioctl arguments: num_tx_ctrlq=%d, "
			"num_tx_dataq=%d, num_rxq=%d, dataq_on:%d\n",
			conf->num_tx_ctrlq, conf->num_tx_dataq,
			conf->num_rxq, conf->dataq_on);

	netdev = alloc_etherdev_mqs(sizeof(struct sn_device),
			conf->num_tx_ctrlq, conf->num_rxq);
	if (!netdev) {
		log_err("alloc_netdev_mqs() failed\n");
		return -ENOMEM;
	}

	if (strcmp(conf->ifname, "") == 0)
		name = "sn%d";
	else
		name = conf->ifname;

	ret = dev_alloc_name(netdev, name);
	if (ret < 0) {
		log_err("failed to alloc name %s\n", name);
		free_netdev(netdev);
		return ret;
	}

	dev = netdev_priv(netdev);
	dev->netdev = netdev;
	dev->num_tx_ctrlq = conf->num_tx_ctrlq;
	dev->num_tx_dataq = conf->num_tx_dataq;
	dev->num_rxq = conf->num_rxq;
	dev->dataq_on = conf->dataq_on;

	sn_set_default_queue_mapping(dev);

	/* Queue assingnment state. */
	/* set to -1 instead so the first allocated queue is 0? */
	atomic_set(&dev->qa_state.next_dataq, 0);

	/* This will disable the default qdisc (mq or pfifo_fast) on the
	 * interface. We don't need qdisc since BESS already has its own.
	 * Also see attach_default_qdiscs() in sch_generic.c */
	/* Loom: I believe that the above comment is incorrect.  Queuing should
	 * be done at the edge and not inside the virtual switch. */
	//netdev->tx_queue_len = 0;

#if (LINUX_VERSION_CODE < KERNEL_VERSION(4,11,9))
	netdev->destructor = sn_netdev_destructor;
#else
	netdev->priv_destructor = sn_netdev_destructor;
#endif

	sn_set_offloads(netdev);

	netdev->netdev_ops = &sn_netdev_ops;
	netdev->ethtool_ops = &sn_ethtool_ops;

	memcpy(netdev->dev_addr, conf->mac_addr, ETH_ALEN);

	ret = sn_alloc_queues(dev, conf + 1,
			conf->bar_size - sizeof(struct sn_conf_space),
			&conf->txq_opts, &conf->rxq_opts);
	if (ret) {
		log_err("sn_alloc_queues() failed\n");
		free_netdev(netdev);
		return ret;
	}

	*dev_ret = dev;
	return 0;
}

int sn_register_netdev(void *bar, struct sn_device *dev)
{
	struct sn_conf_space *conf = bar;

	int ret;

	struct net *net = NULL;		/* network namespace */

	rtnl_lock();

	if (conf->netns_fd >= 0) {
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4,0,0))
		log_err("'netns' option requires Linux kernel 4.0 or higher\n");
		ret = -EINVAL;
		goto fail_free;
#else
		net = get_net_ns_by_fd(conf->netns_fd);
		if (IS_ERR(net)) {
			log_err("invalid or not a net namespace fd %d\n",
					conf->netns_fd);

			ret = PTR_ERR(net);
			goto fail_free;
		}
#endif
	} else if (conf->container_pid) {
		net = get_net_ns_by_pid(conf->container_pid);
		if (IS_ERR(net)) {
			log_err("cannot find namespace of pid %d\n",
					conf->container_pid);

			ret = PTR_ERR(net);
			goto fail_free;
		}
	}

	if (!IS_ERR_OR_NULL(net)) {
		dev_net_set(dev->netdev, net);
		put_net(net);
	}

	ret = register_netdevice(dev->netdev);
	if (ret) {
		log_err("%s: register_netdev() failed (ret = %d)\n",
				dev->netdev->name, ret);
		goto fail_free;
	}

	/* interface "UP" by default */
	dev_open(dev->netdev);

	strcpy(conf->ifname, dev->netdev->name);

	log_info("%s: registered - %pM txq %d rxq %d\n",
			dev->netdev->name,
			dev->netdev->dev_addr,
			dev->netdev->real_num_tx_queues,
			dev->netdev->real_num_rx_queues);

	rtnl_unlock();

	return ret;

fail_free:
	rtnl_unlock();
	free_netdev(dev->netdev);

	return ret;
}

void sn_release_netdev(struct sn_device *dev)
{
	rtnl_lock();

	/* it is possible that the netdev has already been unregistered */
	if (dev && dev->netdev && dev->netdev->reg_state == NETREG_REGISTERED)
		unregister_netdevice(dev->netdev);

	rtnl_unlock();
}

/* This function is called in IRQ context on a remote core.
 * (on the local core, it is in user context)
 * Interrupts are disabled in both cases, anyway.
 *
 * For host mode, this function is invoked by sndrv_ioctl_kick_rx().
 * For guest mode, it should be called in the MSIX handler. */
/* LOOM: This function should handle both RX and TX IRQs.  Also, this function
 * should handle multiple queues being assigned to each core. */
/* LOOM: I think this function call makes an implicit assumption that there is
 * at most one queue per core. I do not think it is necessary for this
 * assumption to always hold. */
void sn_trigger_softirq(void *info)
{
	struct sn_device *dev = info;
	int cpu = raw_smp_processor_id();

	if (unlikely(dev->cpu_to_rxqs[cpu][0] == -1)) {
		struct sn_queue *rx_queue = dev->rx_queues[0];

		rx_queue->rx.stats.interrupts++;
		napi_schedule(&rx_queue->rx.napi);

                /* LOOM: TX? */
	} else {
		/* One core can be mapped to multiple RX queues. Awake them all. */
		int i = 0;
		int rxq;

		while ((rxq = dev->cpu_to_rxqs[cpu][i]) != -1) {
			struct sn_queue *rx_queue = dev->rx_queues[rxq];

			rx_queue->rx.stats.interrupts++;
			napi_schedule(&rx_queue->rx.napi);

			i++;
		}

                /* LOOM: TX? */
	}
}

/* LOOM: In the future, it would be smart to trigger IRQs for individual txqs
 * and cores.  For now, just trigger NAPI for all queues when any queue is
 * refilled. */
void sn_trigger_softirq_tx(void *info)
{
	struct sn_device *dev = info;
	int cpu = raw_smp_processor_id();

	/* LOOM: DEBUG */
	//log_info("%s: sn_trigger_softirq_tx on cpu %d\n",
	//	 dev->netdev->name, cpu);

	if (unlikely(dev->cpu_to_tx_ctrlq[cpu] == -1)) {
		struct sn_queue *tx_ctrl_queue = dev->tx_ctrl_queues[0];

                /* LOOM: DEBUG: */
                //trace_printk("%s: sn_trigger_softirq_tx cpu has no txq?!? "
		//	"for cpu %d queue %d\n", tx_ctrl_queue->dev->netdev->name,
		//	cpu, tx_ctrl_queue->queue_id);

		tx_ctrl_queue->tx_ctrl.stats.interrupts++;
		napi_schedule(&tx_ctrl_queue->tx_ctrl.napi);

	} else {
                /* LOOM: TODO: In the future, multiple TX queues may be mapped
                 * to one core. Awake them all. */
		int txq = dev->cpu_to_tx_ctrlq[cpu];
		struct sn_queue *tx_ctrl_queue = dev->tx_ctrl_queues[txq];

                /* LOOM: DEBUG: */
                //trace_printk("%s: sn_trigger_softirq_tx "
		//	"for cpu %d queue %d\n", tx_ctrl_queue->dev->netdev->name,
		//	cpu, tx_ctrl_queue->queue_id);

		tx_ctrl_queue->tx_ctrl.stats.interrupts++;
		napi_schedule(&tx_ctrl_queue->tx_ctrl.napi);
	}
}

void sn_trigger_softirq_with_qid(void *info, int rxq)
{
	struct sn_device *dev = info;
	struct sn_queue *rx_queue;

	if (rxq < 0 || rxq >= dev->num_rxq) {
		log_err("invalid rxq %d\n", rxq);
		return;
	}

	rx_queue = dev->rx_queues[rxq];

	rx_queue->rx.stats.interrupts++;
	napi_schedule(&rx_queue->rx.napi);
}

void sn_trigger_softirq_with_qid_tx(void *info, int txq)
{
	struct sn_device *dev = info;
	struct sn_queue *tx_ctrl_queue;

	if (txq < 0 || txq >= dev->num_tx_ctrlq) {
		log_err("invalid txq %d\n", txq);
		return;
	}

	tx_ctrl_queue = dev->tx_ctrl_queues[txq];

	tx_ctrl_queue->tx_ctrl.stats.interrupts++;
	napi_schedule(&tx_ctrl_queue->tx_ctrl.napi);
}
