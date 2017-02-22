/*
 * net/sched/sch_loom.c	Loom
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *
 * Authors:	Brent Stephens <brent.stephens@wisc.edu>
 *
 * Note: Loom is an application prorammable software queue. Nothing is
 * supported yet.
 */

#include <linux/init.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/skbuff.h>
#include <net/pkt_sched.h>

/* TODO: Start of PFIFO specific code block. */
static const u8 prio2band[TC_PRIO_MAX + 1] = {
	1, 2, 2, 2, 1, 2, 0, 0 , 1, 1, 1, 1, 1, 1, 1, 1
};

/* 3-band FIFO queue: old style, but should be a bit faster than
   generic prio+fifo combination.
 */
#define LOOM_BANDS 3

/*
 * Private data for a loom scheduler containing:
 * 	- TODO
 */
struct loom_priv {
	u32 bitmap;
	struct qdisc_skb_head q[LOOM_BANDS];
};

/*
 * Convert a bitmap to the first band number where an skb is queued, where:
 * 	bitmap=0 means there are no skbs on any band.
 * 	bitmap=1 means there is an skb on band 0.
 *	bitmap=7 means there are skbs on all 3 bands, etc.
 */
static const int bitmap2band[] = {-1, 0, 1, 0, 2, 0, 1, 0};

static inline struct qdisc_skb_head *band2list(struct loom_priv *priv,
					     int band)
{
	return priv->q + band;
}
/* TODO: End of PFIFO specific code block. */

static int loom_enqueue(struct sk_buff *skb, struct Qdisc *qdisc,
			      struct sk_buff **to_free)
{
	trace_printk("loom enq. skb: %p, skb->len: %d\n", skb, skb->len);

	if (qdisc->q.qlen < qdisc_dev(qdisc)->tx_queue_len) {
		int band = prio2band[skb->priority & TC_PRIO_MAX];
		struct loom_priv *priv = qdisc_priv(qdisc);
		struct qdisc_skb_head *list = band2list(priv, band);

		priv->bitmap |= (1 << band);
		qdisc->q.qlen++;
		return __qdisc_enqueue_tail(skb, qdisc, list);
	}

	return qdisc_drop(skb, qdisc, to_free);
}

static struct sk_buff *loom_dequeue(struct Qdisc *qdisc)
{
	struct loom_priv *priv = qdisc_priv(qdisc);
	int band = bitmap2band[priv->bitmap];

	if (likely(band >= 0)) {
		struct qdisc_skb_head *qh = band2list(priv, band);
		struct sk_buff *skb = __qdisc_dequeue_head(qh);

		if (likely(skb != NULL)) {
			qdisc_qstats_backlog_dec(qdisc, skb);
			qdisc_bstats_update(qdisc, skb);
		}

		qdisc->q.qlen--;
		if (qh->qlen == 0)
			priv->bitmap &= ~(1 << band);

		return skb;
	}

	return NULL;
}

static struct sk_buff *loom_peek(struct Qdisc *qdisc)
{
	struct loom_priv *priv = qdisc_priv(qdisc);
	int band = bitmap2band[priv->bitmap];

	if (band >= 0) {
		struct qdisc_skb_head *qh = band2list(priv, band);

		return qh->head;
	}

	return NULL;
}

static void loom_reset(struct Qdisc *qdisc)
{
	int prio;
	struct loom_priv *priv = qdisc_priv(qdisc);

	for (prio = 0; prio < LOOM_BANDS; prio++)
		__qdisc_reset_queue(band2list(priv, prio));

	priv->bitmap = 0;
	qdisc->qstats.backlog = 0;
	qdisc->q.qlen = 0;
}

static int loom_dump(struct Qdisc *qdisc, struct sk_buff *skb)
{
	struct tc_prio_qopt opt = { .bands = LOOM_BANDS };

	memcpy(&opt.priomap, prio2band, TC_PRIO_MAX + 1);
	if (nla_put(skb, TCA_OPTIONS, sizeof(opt), &opt))
		goto nla_put_failure;
	return skb->len;

nla_put_failure:
	return -1;
}

static int loom_init(struct Qdisc *qdisc, struct nlattr *opt)
{
	int prio;
	struct loom_priv *priv = qdisc_priv(qdisc);

	for (prio = 0; prio < LOOM_BANDS; prio++)
		qdisc_skb_head_init(band2list(priv, prio));

	/* Can by-pass the queue discipline */
	qdisc->flags |= TCQ_F_CAN_BYPASS;
	return 0;
}

static struct Qdisc_ops loom_qdisc_ops __read_mostly = {
	.id		= "loom",
	.priv_size	= sizeof(struct loom_priv),
	.enqueue	= loom_enqueue,
	.dequeue	= loom_dequeue,
	.peek		= loom_peek,
	.init		= loom_init,
	.reset		= loom_reset,
	.dump		= loom_dump,
	.owner		= THIS_MODULE,
};

static int __init loom_module_init(void)
{
	return register_qdisc(&loom_qdisc_ops);
}

static void __exit loom_module_exit(void)
{
	unregister_qdisc(&loom_qdisc_ops);
}

module_init(loom_module_init)
module_exit(loom_module_exit)

MODULE_LICENSE("GPL");
