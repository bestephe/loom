// Copyright (c) 2014-2016, The Regents of the University of California.
// Copyright (c) 2016-2017, Nefeli Networks, Inc.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// * Redistributions of source code must retain the above copyright notice, this
// list of conditions and the following disclaimer.
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

#include "pmd.h"
#include <rte_net.h>

/* LOOM: DPDK SoftNIC specific includes. */
/* TODO: Move into its own driver. */
#include <rte_tm.h>
#include <rte_gso.h>
//#include "../metadata.h"
//#include "../module.h"

#include "../utils/ether.h"
#include "../utils/format.h"

// These likely do not need to be included
#include "../utils/ip.h"
#include "../utils/tcp.h"
#include "../utils/udp.h"

/*!
 * The following are deprecated. Ignore us.
 */
#define SN_TSO_SG 0
#define SN_HW_RXCSUM 0
#define SN_HW_TXCSUM 0

static const struct rte_eth_conf default_eth_conf() {
  struct rte_eth_conf ret = rte_eth_conf();

  ret.link_speeds = ETH_LINK_SPEED_AUTONEG;

  ret.rxmode = {
      .mq_mode = ETH_MQ_RX_RSS,       /* doesn't matter for 1-queue */
      .max_rx_pkt_len = 0,            /* valid only if jumbo is on */
      .split_hdr_size = 0,            /* valid only if HS is on */
      .offloads = 0,                  /* don't use the new interface for RX
                                       * offloads yet */
      .header_split = 0,              /* Header Split */
      .hw_ip_checksum = 1,            /* IP checksum offload */
      .hw_vlan_filter = 0,            /* VLAN filtering */
      .hw_vlan_strip = 0,             /* VLAN strip */
      .hw_vlan_extend = 0,            /* Extended VLAN */
      .jumbo_frame = 0,               /* Jumbo Frame support */
      .hw_strip_crc = 1,              /* CRC stripped by hardware */
      .enable_scatter = 1,            /* scattered RX */
      .enable_lro = 1,                /* large receive offload */
      .hw_timestamp = 1,              /* enable hw timestampping */
      .security = 0,                  /* don't enable rte_security offloads */
      .ignore_offload_bitfield = 0,   /* do not use the "offloads" field yet. */
  };

  ret.rx_adv_conf.rss_conf = {
      .rss_key = nullptr,
      .rss_key_len = 40,
      /* TODO: query rte_eth_dev_info_get() to set this*/
      .rss_hf = ETH_RSS_IP | ETH_RSS_UDP | ETH_RSS_TCP | ETH_RSS_SCTP,
  };

  return ret;
}

void PMDPort::InitDriver() {
  dpdk_port_t num_dpdk_ports = rte_eth_dev_count();

  LOG(INFO) << static_cast<int>(num_dpdk_ports)
            << " DPDK PMD ports have been recognized:";

  for (dpdk_port_t i = 0; i < num_dpdk_ports; i++) {
    struct rte_eth_dev_info dev_info;
    std::string pci_info;
    int numa_node = -1;
    char pmd_name[64]; /* DEBUG: TODO: ensure there are no overflow problems. */
    bess::utils::Ethernet::Address lladdr;

    rte_eth_dev_info_get(i, &dev_info);

    if (dev_info.pci_dev) {
      pci_info = bess::utils::Format(
          "%04hx:%02hhx:%02hhx.%02hhx %04hx:%04hx  ",
          dev_info.pci_dev->addr.domain, dev_info.pci_dev->addr.bus,
          dev_info.pci_dev->addr.devid, dev_info.pci_dev->addr.function,
          dev_info.pci_dev->id.vendor_id, dev_info.pci_dev->id.device_id);
    }

    numa_node = rte_eth_dev_socket_id(static_cast<int>(i));
    rte_eth_macaddr_get(i, reinterpret_cast<ether_addr *>(lladdr.bytes));

    /* LOOM: DEBUG. */
    if (rte_eth_dev_get_name_by_port(i, pmd_name)) {
        LOG(ERROR) << "Cannot find PMD name for DPDK port_id "
            << static_cast<int>(i);
    }

    LOG(INFO) << "DPDK port_id " << static_cast<int>(i) << " name "
              << pmd_name << " ("
              << dev_info.driver_name << ")   RXQ " << dev_info.max_rx_queues
              << " TXQ " << dev_info.max_tx_queues << "  " << lladdr.ToString()
              << "  " << pci_info << " numa_node " << numa_node;
  }
}

// Find a port attached to DPDK by its integral id.
// returns 0 and sets *ret_port_id to "port_id" if the port is valid and
// available.
// returns > 0 on error.
static CommandResponse find_dpdk_port_by_id(dpdk_port_t port_id,
                                            dpdk_port_t *ret_port_id) {
  if (port_id >= RTE_MAX_ETHPORTS) {
    return CommandFailure(EINVAL, "Invalid port id %d", port_id);
  }
  if (rte_eth_devices[port_id].state != RTE_ETH_DEV_ATTACHED) {
    return CommandFailure(ENODEV, "Port id %d is not available", port_id);
  }

  *ret_port_id = port_id;
  return CommandSuccess();
}

// Find a port attached to DPDK by its PCI address.
// returns 0 and sets *ret_port_id to the port_id of the port at PCI address
// "pci" if it is valid and available. *ret_hot_plugged is set to true if the
// device was attached to DPDK as a result of calling this function.
// returns > 0 on error.
static CommandResponse find_dpdk_port_by_pci_addr(const std::string &pci,
                                                  dpdk_port_t *ret_port_id,
                                                  bool *ret_hot_plugged) {
  dpdk_port_t port_id = DPDK_PORT_UNKNOWN;
  struct rte_pci_addr addr;

  if (pci.length() == 0) {
    return CommandFailure(EINVAL, "No PCI address specified");
  }

  if (eal_parse_pci_DomBDF(pci.c_str(), &addr) != 0 &&
      eal_parse_pci_BDF(pci.c_str(), &addr) != 0) {
    return CommandFailure(EINVAL,
                          "PCI address must be like "
                          "dddd:bb:dd.ff or bb:dd.ff");
  }

  dpdk_port_t num_dpdk_ports = rte_eth_dev_count();
  for (dpdk_port_t i = 0; i < num_dpdk_ports; i++) {
    struct rte_eth_dev_info dev_info;
    rte_eth_dev_info_get(i, &dev_info);

    if (dev_info.pci_dev) {
      if (rte_eal_compare_pci_addr(&addr, &dev_info.pci_dev->addr) == 0) {
        port_id = i;
        break;
      }
    }
  }

  // If still not found, maybe the device has not been attached yet
  if (port_id == DPDK_PORT_UNKNOWN) {
    int ret;
    char name[RTE_ETH_NAME_MAX_LEN];
    snprintf(name, RTE_ETH_NAME_MAX_LEN, "%04x:%02x:%02x.%02x", addr.domain,
             addr.bus, addr.devid, addr.function);

    ret = rte_eth_dev_attach(name, &port_id);

    if (ret < 0) {
      return CommandFailure(ENODEV, "Cannot attach PCI device %s", name);
    }

    *ret_hot_plugged = true;
  }

  *ret_port_id = port_id;
  return CommandSuccess();
}

// Find a DPDK vdev by name.
// returns 0 and sets *ret_port_id to the port_id of "vdev" if it is valid and
// available. *ret_hot_plugged is set to true if the device was attached to
// DPDK as a result of calling this function.
// returns > 0 on error.
static CommandResponse find_dpdk_vdev(const std::string &vdev,
                                      dpdk_port_t *ret_port_id,
                                      bool *ret_hot_plugged) {
  dpdk_port_t port_id = DPDK_PORT_UNKNOWN;

  if (vdev.length() == 0) {
    return CommandFailure(EINVAL, "No vdev specified");
  }

  const char *name = vdev.c_str();
  int ret = rte_eth_dev_attach(name, &port_id);

  if (ret < 0) {
    return CommandFailure(ENODEV, "Cannot attach vdev %s", name);
  }

  *ret_hot_plugged = true;
  *ret_port_id = port_id;
  return CommandSuccess();
}

/* LOOM: DPDK SoftNIC Defines. */
/* TODO: move somewhere else */
#define SUBPORT_NODES_PER_PORT		1
#define PIPE_NODES_PER_SUBPORT		4
#define TC_NODES_PER_PIPE			4
#define QUEUE_NODES_PER_TC			4

#define NUM_PIPE_NODES						\
	(SUBPORT_NODES_PER_PORT * PIPE_NODES_PER_SUBPORT)

#define NUM_TC_NODES						\
	(NUM_PIPE_NODES * TC_NODES_PER_PIPE)

#define ROOT_NODE_ID				1000000
#define SUBPORT_NODES_START_ID		900000
#define PIPE_NODES_START_ID			800000
#define TC_NODES_START_ID			700000

/* TM Hierarchy Levels */
enum tm_hierarchy_level {
	TM_NODE_LEVEL_PORT = 0,
	TM_NODE_LEVEL_SUBPORT,
	TM_NODE_LEVEL_PIPE,
	TM_NODE_LEVEL_TC,
	TM_NODE_LEVEL_QUEUE,
	TM_NODE_LEVEL_MAX,
};

struct tm_hierarchy {
	/* TM Nodes */
	uint32_t root_node_id;
	uint32_t subport_node_id[SUBPORT_NODES_PER_PORT];
	uint32_t pipe_node_id[SUBPORT_NODES_PER_PORT][PIPE_NODES_PER_SUBPORT];
	uint32_t tc_node_id[NUM_PIPE_NODES][TC_NODES_PER_PIPE];
	uint32_t queue_node_id[NUM_TC_NODES][QUEUE_NODES_PER_TC];

	/* TM Hierarchy Nodes Shaper Rates */
	uint32_t root_node_shaper_rate;
	uint32_t subport_node_shaper_rate;
	uint32_t pipe_node_shaper_rate;
	uint32_t tc_node_shaper_rate;
	uint32_t tc_node_shared_shaper_rate;

	uint32_t n_shapers;
};

#define RTE_SCHED_PORT_HIERARCHY(subport, pipe,           \
	traffic_class, queue, color)                          \
	((((uint64_t) (queue)) & 0x3) |                       \
	((((uint64_t) (traffic_class)) & 0x3) << 2) |         \
	((((uint64_t) (color)) & 0x3) << 4) |                 \
	((((uint64_t) (subport)) & 0xFFFF) << 16) |           \
	((((uint64_t) (pipe)) & 0xFFFFFFFF) << 32))

#define STATS_MASK_DEFAULT					\
	(RTE_TM_STATS_N_PKTS |					\
	RTE_TM_STATS_N_BYTES |					\
	RTE_TM_STATS_N_PKTS_GREEN_DROPPED |			\
	RTE_TM_STATS_N_BYTES_GREEN_DROPPED)

#define STATS_MASK_QUEUE					\
	(STATS_MASK_DEFAULT |					\
	RTE_TM_STATS_N_PKTS_QUEUED)

#define BYTES_IN_MBPS				(1000 * 1000 / 8)
#define TOKEN_BUCKET_SIZE			1000000

static void
set_tm_hiearchy_nodes_shaper_rate(dpdk_port_t port_id, struct tm_hierarchy *h)
{
	struct rte_eth_link link_params;
	uint64_t tm_port_rate;

	memset(&link_params, 0, sizeof(link_params));

	rte_eth_link_get(port_id, &link_params);
	tm_port_rate = (uint64_t)link_params.link_speed * BYTES_IN_MBPS;

	if (tm_port_rate > UINT32_MAX)
		tm_port_rate = UINT32_MAX;

	/* Set tm hierarchy shapers rate */
	h->root_node_shaper_rate = tm_port_rate;
	h->subport_node_shaper_rate = tm_port_rate;
	h->pipe_node_shaper_rate
		= h->subport_node_shaper_rate;
	h->tc_node_shaper_rate = h->pipe_node_shaper_rate;
	h->tc_node_shared_shaper_rate = h->subport_node_shaper_rate;
}

static int
softport_tm_root_node_add(dpdk_port_t port_id, struct tm_hierarchy *h,
	struct rte_tm_error *error)
{
	struct rte_tm_node_params rnp;
	struct rte_tm_shaper_params rsp;
	uint32_t priority, weight, level_id, shaper_profile_id;

	memset(&rsp, 0, sizeof(struct rte_tm_shaper_params));
	memset(&rnp, 0, sizeof(struct rte_tm_node_params));

	/* Shaper profile Parameters */
	rsp.peak.rate = h->root_node_shaper_rate;
	rsp.peak.size = TOKEN_BUCKET_SIZE;
	rsp.pkt_length_adjust = RTE_TM_ETH_FRAMING_OVERHEAD_FCS;
	shaper_profile_id = 0;

	if (rte_tm_shaper_profile_add(port_id, shaper_profile_id,
		&rsp, error)) {
		LOG(ERROR) << bess::utils::Format("%s ERROR(%d)-%s!(shaper_id %u)\n ",
			__func__, error->type, error->message,
			shaper_profile_id);
		return -1;
	}

	/* Root Node Parameters */
	h->root_node_id = ROOT_NODE_ID;
	weight = 1;
	priority = 0;
	level_id = TM_NODE_LEVEL_PORT;
	rnp.shaper_profile_id = shaper_profile_id;
	rnp.nonleaf.n_sp_priorities = 1;
	rnp.stats_mask = STATS_MASK_DEFAULT;

	/* Add Node to TM Hierarchy */
	if (rte_tm_node_add(port_id, h->root_node_id, RTE_TM_NODE_ID_NULL,
		priority, weight, level_id, &rnp, error)) {
		LOG(ERROR) << bess::utils::Format("%s ERROR(%d)-%s!(node_id %u, parent_id %u, level %u)\n",
			__func__, error->type, error->message,
			h->root_node_id, RTE_TM_NODE_ID_NULL,
			level_id);
		return -1;
	}
	/* Update */
	h->n_shapers++;

	LOG(INFO) << bess::utils::Format("  Root node added (Start id %u, Count %u, level %u)\n",
		h->root_node_id, 1, level_id);

	return 0;
}

static int
softport_tm_subport_node_add(dpdk_port_t port_id, struct tm_hierarchy *h,
	struct rte_tm_error *error)
{
	uint32_t subport_parent_node_id, subport_node_id = 0;
	struct rte_tm_node_params snp;
	struct rte_tm_shaper_params ssp;
	uint32_t priority, weight, level_id, shaper_profile_id;
	uint32_t i;

	memset(&ssp, 0, sizeof(struct rte_tm_shaper_params));
	memset(&snp, 0, sizeof(struct rte_tm_node_params));

	shaper_profile_id = h->n_shapers;

	/* Add Shaper Profile to TM Hierarchy */
	for (i = 0; i < SUBPORT_NODES_PER_PORT; i++) {
		ssp.peak.rate = h->subport_node_shaper_rate;
		ssp.peak.size = TOKEN_BUCKET_SIZE;
		ssp.pkt_length_adjust = RTE_TM_ETH_FRAMING_OVERHEAD_FCS;

		if (rte_tm_shaper_profile_add(port_id, shaper_profile_id,
			&ssp, error)) {
			LOG(ERROR) << bess::utils::Format("%s ERROR(%d)-%s!(shaper_id %u)\n ",
				__func__, error->type, error->message,
				shaper_profile_id);
			return -1;
		}

		/* Node Parameters */
		h->subport_node_id[i] = SUBPORT_NODES_START_ID + i;
		subport_parent_node_id = h->root_node_id;
		weight = 1;
		priority = 0;
		level_id = TM_NODE_LEVEL_SUBPORT;
		snp.shaper_profile_id = shaper_profile_id;
		snp.nonleaf.n_sp_priorities = 1;
		snp.stats_mask = STATS_MASK_DEFAULT;

		/* Add Node to TM Hiearchy */
		if (rte_tm_node_add(port_id,
				h->subport_node_id[i],
				subport_parent_node_id,
				priority, weight,
				level_id,
				&snp,
				error)) {
			LOG(ERROR) << bess::utils::Format("%s ERROR(%d)-%s!(node %u,parent %u,level %u)\n",
					__func__,
					error->type,
					error->message,
					h->subport_node_id[i],
					subport_parent_node_id,
					level_id);
			return -1;
		}
		shaper_profile_id++;
		subport_node_id++;
	}
	/* Update */
	h->n_shapers = shaper_profile_id;

	LOG(INFO) << bess::utils::Format("  Subport nodes added (Start id %u, Count %u, level %u)\n",
		h->subport_node_id[0], SUBPORT_NODES_PER_PORT, level_id);

	return 0;
}

static int
softport_tm_pipe_node_add(dpdk_port_t port_id, struct tm_hierarchy *h,
	struct rte_tm_error *error)
{
	uint32_t pipe_parent_node_id;
	struct rte_tm_node_params pnp;
	struct rte_tm_shaper_params psp;
	uint32_t priority, weight, level_id, shaper_profile_id;
	uint32_t i, j;

	memset(&psp, 0, sizeof(struct rte_tm_shaper_params));
	memset(&pnp, 0, sizeof(struct rte_tm_node_params));

	shaper_profile_id = h->n_shapers;

	/* Shaper Profile Parameters */
	psp.peak.rate = h->pipe_node_shaper_rate;
	psp.peak.size = TOKEN_BUCKET_SIZE;
	psp.pkt_length_adjust = RTE_TM_ETH_FRAMING_OVERHEAD_FCS;

	/* Pipe Node Parameters */
	weight = 1;
	priority = 0;
	level_id = TM_NODE_LEVEL_PIPE;
	pnp.nonleaf.n_sp_priorities = 4;
	pnp.stats_mask = STATS_MASK_DEFAULT;

	/* Add Shaper Profiles and Nodes to TM Hierarchy */
	for (i = 0; i < SUBPORT_NODES_PER_PORT; i++) {
		for (j = 0; j < PIPE_NODES_PER_SUBPORT; j++) {
			if (rte_tm_shaper_profile_add(port_id,
				shaper_profile_id, &psp, error)) {
				LOG(ERROR) << bess::utils::Format("%s ERROR(%d)-%s!(shaper_id %u)\n ",
					__func__, error->type, error->message,
					shaper_profile_id);
				return -1;
			}
			pnp.shaper_profile_id = shaper_profile_id;
			pipe_parent_node_id = h->subport_node_id[i];
			h->pipe_node_id[i][j] = PIPE_NODES_START_ID +
				(i * PIPE_NODES_PER_SUBPORT) + j;

			if (rte_tm_node_add(port_id,
					h->pipe_node_id[i][j],
					pipe_parent_node_id,
					priority, weight, level_id,
					&pnp,
					error)) {
				LOG(ERROR) << bess::utils::Format("%s ERROR(%d)-%s!(node %u,parent %u )\n",
					__func__,
					error->type,
					error->message,
					h->pipe_node_id[i][j],
					pipe_parent_node_id);

				return -1;
			}
			shaper_profile_id++;
		}
	}
	/* Update */
	h->n_shapers = shaper_profile_id;

	LOG(INFO) << bess::utils::Format("  Pipe nodes added (Start id %u, Count %u, level %u)\n",
		h->pipe_node_id[0][0], NUM_PIPE_NODES, level_id);

	return 0;
}

static int
softport_tm_tc_node_add(dpdk_port_t port_id, struct tm_hierarchy *h,
	struct rte_tm_error *error)
{
	uint32_t tc_parent_node_id;
	struct rte_tm_node_params tnp;
	struct rte_tm_shaper_params tsp, tssp;
	uint32_t shared_shaper_profile_id[TC_NODES_PER_PIPE];
	uint32_t priority, weight, level_id, shaper_profile_id;
	uint32_t pos, n_tc_nodes, i, j, k;

	memset(&tsp, 0, sizeof(struct rte_tm_shaper_params));
	memset(&tssp, 0, sizeof(struct rte_tm_shaper_params));
	memset(&tnp, 0, sizeof(struct rte_tm_node_params));

	shaper_profile_id = h->n_shapers;

	/* Private Shaper Profile (TC) Parameters */
	tsp.peak.rate = h->tc_node_shaper_rate;
	tsp.peak.size = TOKEN_BUCKET_SIZE;
	tsp.pkt_length_adjust = RTE_TM_ETH_FRAMING_OVERHEAD_FCS;

	/* Shared Shaper Profile (TC) Parameters */
	tssp.peak.rate = h->tc_node_shared_shaper_rate;
	tssp.peak.size = TOKEN_BUCKET_SIZE;
	tssp.pkt_length_adjust = RTE_TM_ETH_FRAMING_OVERHEAD_FCS;

	/* TC Node Parameters */
	weight = 1;
	level_id = TM_NODE_LEVEL_TC;
	tnp.n_shared_shapers = 1; /* TODO: remove the shared shapers. */
	tnp.nonleaf.n_sp_priorities = 1;
	tnp.stats_mask = STATS_MASK_DEFAULT;

	/* Add Shared Shaper Profiles to TM Hierarchy */
	for (i = 0; i < TC_NODES_PER_PIPE; i++) {
		shared_shaper_profile_id[i] = shaper_profile_id;

		if (rte_tm_shaper_profile_add(port_id,
			shared_shaper_profile_id[i], &tssp, error)) {
			LOG(ERROR) << bess::utils::Format("%s ERROR(%d)-%s!(Shared shaper profileid %u)\n",
				__func__, error->type, error->message,
				shared_shaper_profile_id[i]);

			return -1;
		}
		if (rte_tm_shared_shaper_add_update(port_id,  i,
			shared_shaper_profile_id[i], error)) {
			LOG(ERROR) << bess::utils::Format("%s ERROR(%d)-%s!(Shared shaper id %u)\n",
				__func__, error->type, error->message, i);

			return -1;
		}
		shaper_profile_id++;
	}

	/* Add Shaper Profiles and Nodes to TM Hierarchy */
	n_tc_nodes = 0;
	for (i = 0; i < SUBPORT_NODES_PER_PORT; i++) {
		for (j = 0; j < PIPE_NODES_PER_SUBPORT; j++) {
			for (k = 0; k < TC_NODES_PER_PIPE ; k++) {
				priority = k;
				tc_parent_node_id = h->pipe_node_id[i][j];
				tnp.shared_shaper_id =
					(uint32_t *)calloc(1, sizeof(uint32_t));
				tnp.shared_shaper_id[0] = k;
				pos = j + (i * PIPE_NODES_PER_SUBPORT);
				h->tc_node_id[pos][k] =
					TC_NODES_START_ID + n_tc_nodes;

				if (rte_tm_shaper_profile_add(port_id,
					shaper_profile_id, &tsp, error)) {
					LOG(ERROR) << bess::utils::Format("%s ERROR(%d)-%s!(shaper %u)\n",
						__func__, error->type,
						error->message,
						shaper_profile_id);

					return -1;
				}
				tnp.shaper_profile_id = shaper_profile_id;
				if (rte_tm_node_add(port_id,
						h->tc_node_id[pos][k],
						tc_parent_node_id,
						priority, weight,
						level_id,
						&tnp, error)) {
					LOG(ERROR) << bess::utils::Format("%s ERROR(%d)-%s!(node id %u)\n",
						__func__,
						error->type,
						error->message,
						h->tc_node_id[pos][k]);

					return -1;
				}
				shaper_profile_id++;
				n_tc_nodes++;
			}
		}
	}
	/* Update */
	h->n_shapers = shaper_profile_id;

	LOG(INFO) << bess::utils::Format("  TC nodes added (Start id %u, Count %u, level %u)\n",
		h->tc_node_id[0][0], n_tc_nodes, level_id);

	return 0;
}

static int
softport_tm_queue_node_add(dpdk_port_t port_id, struct tm_hierarchy *h,
	struct rte_tm_error *error)
{
	uint32_t queue_parent_node_id;
	struct rte_tm_node_params qnp;
	uint32_t priority, weight, level_id, pos;
	uint32_t n_queue_nodes, i, j, k;

	memset(&qnp, 0, sizeof(struct rte_tm_node_params));

	/* Queue Node Parameters */
	priority = 0;
	weight = 1;
	level_id = TM_NODE_LEVEL_QUEUE;
	qnp.shaper_profile_id = RTE_TM_SHAPER_PROFILE_ID_NONE;
	qnp.leaf.cman = RTE_TM_CMAN_TAIL_DROP;
	qnp.stats_mask = STATS_MASK_QUEUE;

	/* Add Queue Nodes to TM Hierarchy */
	n_queue_nodes = 0;
	for (i = 0; i < NUM_PIPE_NODES; i++) {
		for (j = 0; j < TC_NODES_PER_PIPE; j++) {
			queue_parent_node_id = h->tc_node_id[i][j];
			for (k = 0; k < QUEUE_NODES_PER_TC; k++) {
				pos = j + (i * TC_NODES_PER_PIPE);
				h->queue_node_id[pos][k] = n_queue_nodes;
				if (rte_tm_node_add(port_id,
						h->queue_node_id[pos][k],
						queue_parent_node_id,
						priority,
						weight,
						level_id,
						&qnp, error)) {
					LOG(ERROR) << bess::utils::Format("%s ERROR(%d)-%s!(node %u)\n",
						__func__,
						error->type,
						error->message,
						h->queue_node_id[pos][k]);

					return -1;
				}
				n_queue_nodes++;
			}
		}
	}
	LOG(INFO) << bess::utils::Format("  Queue nodes added (Start id %u, Count %u, level %u)\n",
		h->queue_node_id[0][0], n_queue_nodes, level_id);

	return 0;
}

/* LOOM: Start by statically configuring DPDK TM hierarchy.
 *  TODO: Eventually do:
 *   1) Move DPDK SoftNIC code to its on driver.
 *   2)configure via bessctl.
 */
static void setup_dpdk_softnic_hierarchy(dpdk_port_t port_id)
{
  struct tm_hierarchy h;
  struct rte_tm_error error;
  int status;

  memset(&h, 0, sizeof(struct tm_hierarchy));

  /* TM hierarchy shapers rate */
  set_tm_hiearchy_nodes_shaper_rate(port_id, &h);

  /* Add root node (level 0) */
  status = softport_tm_root_node_add(port_id, &h, &error);
  if (status)
          goto hierarchy_specify_error;

  /* Add subport node (level 1) */
  status = softport_tm_subport_node_add(port_id, &h, &error);
  if (status)
          goto hierarchy_specify_error;

  /* Add pipe nodes (level 2) */
  status = softport_tm_pipe_node_add(port_id, &h, &error);
  if (status)
          goto hierarchy_specify_error;

  /* Add traffic class nodes (level 3) */
  status = softport_tm_tc_node_add(port_id, &h, &error);
  if (status)
          goto hierarchy_specify_error;

  /* Add queue nodes (level 4) */
  status = softport_tm_queue_node_add(port_id, &h, &error);
  if (status)
          goto hierarchy_specify_error;

hierarchy_specify_error:
  if (status) {
          LOG(ERROR) << bess::utils::Format("  TM Hierarchy built error(%d) - %s\n",
                  error.type, error.message);
          return;
  }
  printf("\n  TM Hierarchy Specified!\n\v");

  /* TM hierarchy commit */
  status = rte_tm_hierarchy_commit(port_id, 0, &error);
  if (status) {
    LOG(ERROR) << "  Hierarchy commit error(" << error.type
      << ") - " << error.message;
    return;
  }
  LOG(INFO) << "  Hierarchy Committed (port " << port_id << ")!";
}

//static void
//pkt_metadata_set(bess::Packet **pkts, uint32_t n_pkts)
//void PMDPort::PktMetadataSet(bess::Packet **pkts, uint32_t n_pkts) {
//  uint32_t i;
//  for (i = 0; i < n_pkts; i++)	{
//          bess::Packet *pkt = pkts[i];
//          struct rte_mbuf *rte_pkt = &pkt->as_rte_mbuf();
//          uint8_t priority = get_attr<uint8_t>(this, 0, pkt);
//
//          LOG(INFO) << "Packet priority: " << priority;
//
//          uint64_t pkt_sched = RTE_SCHED_PORT_HIERARCHY(0,
//                                          0,
//                                          0,
//                                          0,
//                                          0);
//
//          rte_pkt->hash.sched.lo = pkt_sched & 0xFFFFFFFF;
//          rte_pkt->hash.sched.hi = pkt_sched >> 32;
//  }
//}

CommandResponse PMDPort::Init(const bess::pb::PMDPortArg &arg) {
  dpdk_port_t ret_port_id = DPDK_PORT_UNKNOWN;

  struct rte_eth_dev_info dev_info;
  struct rte_eth_conf eth_conf;
  struct rte_eth_rxconf eth_rxconf;
  struct rte_eth_txconf eth_txconf;

  int num_txq = num_queues[PACKET_DIR_OUT];
  int num_rxq = num_queues[PACKET_DIR_INC];

  int ret;

  int i;

  int numa_node = -1;

  CommandResponse err;
  switch (arg.port_case()) {
    case bess::pb::PMDPortArg::kPortId: {
      err = find_dpdk_port_by_id(arg.port_id(), &ret_port_id);
      break;
    }
    case bess::pb::PMDPortArg::kPci: {
      err = find_dpdk_port_by_pci_addr(arg.pci(), &ret_port_id, &hot_plugged_);
      break;
    }
    case bess::pb::PMDPortArg::kVdev: {
      err = find_dpdk_vdev(arg.vdev(), &ret_port_id, &hot_plugged_);
      break;
    }
    default:
      return CommandFailure(EINVAL, "No port specified");
  }

  if (err.error().code() != 0) {
    return err;
  }

  if (ret_port_id == DPDK_PORT_UNKNOWN) {
    return CommandFailure(ENOENT, "Port not found");
  }

  eth_conf = default_eth_conf();
  if (arg.loopback()) {
    eth_conf.lpbk_mode = 1;
  }

  /* Use defaut rx/tx configuration as provided by PMD drivers,
   * with minor tweaks */
  rte_eth_dev_info_get(ret_port_id, &dev_info);

  if (dev_info.driver_name) {
    driver_ = dev_info.driver_name;
  }


  eth_rxconf = dev_info.default_rxconf;

  /* #36: em driver does not allow rx_drop_en enabled */
  if (driver_ != "net_e1000_em") {
    eth_rxconf.rx_drop_en = 1;
  }

  LOG(INFO) << "PMD Driver: " << driver_;
  if (driver_ == "net_virtio_user") {
    LOG(INFO) << "Disabling LRO for virtio_user";
    eth_conf.rxmode.enable_lro = 0;
    eth_conf.rxmode.hw_ip_checksum = 0;
    needs_tso_csum_ = false;
  }
  /* DEBUG: TODO: REMOVE */
  else {
    LOG(INFO) << "DEBUG Always Disabling LRO";
    eth_conf.rxmode.enable_lro = 0;
    eth_conf.rxmode.hw_ip_checksum = 0;
  }
  if (driver_ == "net_ixgbe") {
    needs_tso_csum_ = true;
  }
  /* LOOM: XXX: This is a bad hack! Assumes DPDK SoftNIC is only used with a
   * "net_ixgbe" backend. */
  if (driver_ == "net_softnic") {
    needs_tso_csum_ = true;
    setup_dpdk_softnic_hierarchy(ret_port_id);
    //AddMetadataAttr("priority", 1, AccessMode::kRead);
  }

  /* LOOM: Requeuing hack for using the DPDK GSO library. */
  rq_.rx_rq_pos = 0;
  rq_.rx_rq_pkts_len = 0;
  rq_.gso_segs_pos = 0;
  rq_.gso_segs_len = 0;

  eth_txconf = dev_info.default_txconf;
  eth_txconf.txq_flags = ETH_TXQ_FLAGS_NOVLANOFFL;
  //eth_txconf.txq_flags = ETH_TXQ_FLAGS_NOVLANOFFL |
  //                       ETH_TXQ_FLAGS_NOXSUMS * (1 - SN_HW_TXCSUM);
  //eth_txconf.txq_flags = ETH_TXQ_FLAGS_NOVLANOFFL |
  //                       ETH_TXQ_FLAGS_NOMULTSEGS * (1 - SN_TSO_SG) |
  //                       ETH_TXQ_FLAGS_NOXSUMS * (1 - SN_HW_TXCSUM);

  ret = rte_eth_dev_configure(ret_port_id, num_rxq, num_txq, &eth_conf);
  if (ret != 0) {
    return CommandFailure(-ret, "rte_eth_dev_configure() failed");
  }
  rte_eth_promiscuous_enable(ret_port_id);

  /* LOOM: XXX: Small TX Queues Hack. (allow queue_size == 32) */
  LOG(INFO) << bess::utils::Format("default tx_rs_thresh: %d, tx_free_thresh: %d",
    eth_txconf.tx_rs_thresh, eth_txconf.tx_free_thresh);
  eth_txconf.tx_rs_thresh = 4;
  eth_txconf.tx_free_thresh = 8;

  // NOTE: As of DPDK 17.02, TX queues should be initialized first.
  // Otherwise the DPDK virtio PMD will crash in rte_eth_rx_burst() later.
  for (i = 0; i < num_txq; i++) {
    int sid = 0; /* XXX */

    ret = rte_eth_tx_queue_setup(ret_port_id, i, queue_size[PACKET_DIR_OUT],
                                 sid, &eth_txconf);
    if (ret != 0) {
      return CommandFailure(-ret, "rte_eth_tx_queue_setup() failed");
    }
  }

  for (i = 0; i < num_rxq; i++) {
    int sid = rte_eth_dev_socket_id(ret_port_id);

    /* if socket_id is invalid, set to 0 */
    if (sid < 0 || sid > RTE_MAX_NUMA_NODES) {
      sid = 0;
    }

    ret =
        rte_eth_rx_queue_setup(ret_port_id, i, queue_size[PACKET_DIR_INC], sid,
                               &eth_rxconf, bess::get_pframe_pool_socket(sid));
    if (ret != 0) {
      return CommandFailure(-ret, "rte_eth_rx_queue_setup() failed");
    }
  }

  ret = rte_eth_dev_start(ret_port_id);
  if (ret != 0) {
    return CommandFailure(-ret, "rte_eth_dev_start() failed");
  }

  dpdk_port_id_ = ret_port_id;

  ret = rte_eth_dev_get_mtu(dpdk_port_id_, &mtu_);
  if (ret != 0) {
    return CommandFailure(-ret, "rte_eth_dev_get_mtu() failed");
  }

  numa_node = rte_eth_dev_socket_id(static_cast<int>(ret_port_id));
  node_placement_ =
      numa_node == -1 ? UNCONSTRAINED_SOCKET : (1ull << numa_node);

  rte_eth_macaddr_get(dpdk_port_id_, reinterpret_cast<ether_addr *>(&mac_addr));

  // Reset hardware stat counters, as they may still contain previous data
  CollectStats(true);

  return CommandSuccess();
}

void PMDPort::DeInit() {
  /* LOOM: DEBUG: */
  if (!rte_eth_dev_is_valid_port(dpdk_port_id_)) {
    LOG(ERROR) << "DeInit on non-valid port id:" << dpdk_port_id_;
  }

  rte_eth_dev_stop(dpdk_port_id_);

  if (hot_plugged_) {
    char name[RTE_ETH_NAME_MAX_LEN];
    int ret;

    rte_eth_dev_close(dpdk_port_id_);
    ret = rte_eth_dev_detach(dpdk_port_id_, name);
    if (ret < 0) {
      LOG(WARNING) << "rte_eth_dev_detach(" << static_cast<int>(dpdk_port_id_)
                   << ") failed: " << rte_strerror(-ret);
    }
  }
}

void PMDPort::CollectStats(bool reset) {
  struct rte_eth_stats stats;
  int ret;

  packet_dir_t dir;
  queue_t qid;

  if (reset) {
    rte_eth_stats_reset(dpdk_port_id_);
    return;
  }

  ret = rte_eth_stats_get(dpdk_port_id_, &stats);
  if (ret < 0) {
    LOG(ERROR) << "rte_eth_stats_get(" << static_cast<int>(dpdk_port_id_)
               << ") failed: " << rte_strerror(-ret);
    return;
  }

  VLOG(1) << bess::utils::Format(
      "PMD port %d: ipackets %" PRIu64 " opackets %" PRIu64 " ibytes %" PRIu64
      " obytes %" PRIu64 " imissed %" PRIu64 " ierrors %" PRIu64
      " oerrors %" PRIu64 " rx_nombuf %" PRIu64,
      dpdk_port_id_, stats.ipackets, stats.opackets, stats.ibytes, stats.obytes,
      stats.imissed, stats.ierrors, stats.oerrors, stats.rx_nombuf);

  port_stats_.inc.dropped = stats.imissed;

  // i40e PMD driver and ixgbevf don't support per-queue stats
  if (driver_ == "net_i40e" || driver_ == "net_i40e_vf" ||
      driver_ == "net_ixgbe_vf") {
    // NOTE:
    // - if link is down, tx bytes won't increase
    // - if destination MAC address is incorrect, rx pkts won't increase
    port_stats_.inc.packets = stats.ipackets;
    port_stats_.inc.bytes = stats.ibytes;
    port_stats_.out.packets = stats.opackets;
    port_stats_.out.bytes = stats.obytes;
  } else {
    dir = PACKET_DIR_INC;
    for (qid = 0; qid < num_queues[dir]; qid++) {
      queue_stats[dir][qid].packets = stats.q_ipackets[qid];
      queue_stats[dir][qid].bytes = stats.q_ibytes[qid];
      queue_stats[dir][qid].dropped = stats.q_errors[qid];
    }

    dir = PACKET_DIR_OUT;
    for (qid = 0; qid < num_queues[dir]; qid++) {
      queue_stats[dir][qid].packets = stats.q_opackets[qid];
      queue_stats[dir][qid].bytes = stats.q_obytes[qid];
    }
  }
}

/* Assumes ETH/IPv4/TCP (and no VLAN, tunneling, etc.) */
static void config_pkt_offloads(bess::Packet *pkt, uint16_t mtu) {
  using bess::utils::Ethernet;
  using bess::utils::Ipv4;
  using bess::utils::Tcp;
  using bess::utils::Udp;
  using bess::utils::be16_t;

  struct rte_mbuf *m = &pkt->as_rte_mbuf();

  Ethernet *eth = pkt->head_data<Ethernet *>();
  if (eth->ether_type != be16_t(Ethernet::Type::kIpv4))
    return;
  Ipv4 *ip = reinterpret_cast<Ipv4 *>(eth + 1);
  uint16_t ip_header_len = (ip->header_length) << 2;
  if (ip->protocol != Ipv4::Proto::kTcp)
    return;
  Tcp *tcp =
      reinterpret_cast<Tcp *>(reinterpret_cast<uint8_t *>(ip) + ip_header_len);

  uint16_t eth_len = sizeof(*eth);
  //size_t ip_len = ip->length.value();
  uint16_t tcp_len = (tcp->offset << 2);
  uint16_t segsz = mtu - (eth_len + ip_header_len + tcp_len);
  //LOG(INFO) << "segsz: " << segsz << ", mtu: " << mtu;

  m->l2_len = eth_len;
  m->l3_len = ip_header_len;
  m->l4_len = tcp_len;
  m->tso_segsz = segsz;
  m->ol_flags = PKT_TX_IPV4 | PKT_TX_IP_CKSUM | PKT_TX_TCP_SEG;

  rte_net_intel_cksum_prepare(m); 
}

int segment_gso_pkt(bess::Packet *pkt, bess::Packet **segs, int segs_len) {
  int ret;

  /* GSO context. */
  struct rte_gso_ctx gso_ctx;
  struct rte_mempool *pool = bess::get_pframe_pool();
  gso_ctx.direct_pool = pool;
  gso_ctx.indirect_pool = pool;
  gso_ctx.gso_types = DEV_TX_OFFLOAD_TCP_TSO | DEV_TX_OFFLOAD_VXLAN_TNL_TSO |
    DEV_TX_OFFLOAD_GRE_TNL_TSO;
  //gso_ctx.gso_size = ETHER_MAX_LEN - ETHER_CRC_LEN; /* TODO: MTU */
  gso_ctx.gso_size = 8192; /* TODO: MTU */
  gso_ctx.flag = 0;

  //LOG(INFO) << "Attempting GSO";

  /* Set appropriate flags for GSO */
  config_pkt_offloads(pkt, gso_ctx.gso_size);

  ret = rte_gso_segment(&pkt->as_rte_mbuf(), &gso_ctx, (struct rte_mbuf **)segs, segs_len);
  if (ret > 0) {
    //LOG(INFO) << bess::utils::Format("Created %d segments", ret);

    /* Disable offload request for now. */
    int i;
    for (i = 0; i < ret; i++) {
      struct rte_mbuf *m = &segs[i]->as_rte_mbuf();
      m->ol_flags = 0;
    }

    return ret;
  } else {
    LOG(ERROR) << "Unable to segment packet";
    rte_pktmbuf_free(&segs[0]->as_rte_mbuf());

    return 0;
  }
}

int PMDPort::RecvPackets(queue_t qid, bess::Packet **pkts, int cnt) {
  int ret;

/* XXX: DEBUG: Removing software GSO for now. */
#if 0
  /* LOOM: XXX: Test out using GSO. */
#if 0
  if (driver_ == "net_virtio_user") {
    /* Receive a burst of packets if needed. */
    if (rq_.rx_rq_pkts_len == 0) {
      assert(rq_.rx_rq_pos == 0);

      ret = rte_eth_rx_burst(dpdk_port_id_, qid, (struct rte_mbuf **)rq_.rx_rq_pkts,
        8); /* TODO: Use a different (configurable) burst! */
      assert(ret >= 0);
      rq_.rx_rq_pkts_len = ret;
    }

    int batch_cnt = 0;
    while (rq_.rx_rq_pos < rq_.rx_rq_pkts_len && batch_cnt < cnt) {
      /* Generate more segments if needed. */
      if (rq_.gso_segs_len == 0) {
        assert(rq_.gso_segs_pos == 0);

        ret = segment_gso_pkt(rq_.rx_rq_pkts[rq_.rx_rq_pos], rq_.gso_segs, GSO_MAX_PKT_BURST);
        assert(ret >= 0);
        rq_.rx_rq_pos++;
        rq_.gso_segs_len = ret;
        rq_.gso_segs_pos = 0;
      }

      /* Fill the batch with requeued packets. */
      int rq_cnt = std::min(rq_.gso_segs_len - rq_.gso_segs_pos, cnt - batch_cnt);
      for (int i = 0; i < rq_cnt; i++) {
        pkts[batch_cnt] = rq_.gso_segs[rq_.gso_segs_pos];
        batch_cnt++;
        rq_.gso_segs_pos++;
        assert(batch_cnt < cnt);
        assert(rq_.gso_segs_pos <= rq_.gso_segs_len);
      }

      /* Reset the GSO segs if necessary. */
      if (rq_.gso_segs_pos >= rq_.gso_segs_len) {
        rq_.gso_segs_pos = 0;
        rq_.gso_segs_len = 0;
      }
      /* Reset the RX Requeue if necessary. */
      if (rq_.rx_rq_pos >= rq_.rx_rq_pkts_len) {
        rq_.rx_rq_pos = 0;
        rq_.rx_rq_pkts_len = 0;
      }

      /* DEBUG. */
      //LOG(INFO) << bess::utils::Format(
      //  "Recv a batch of %d packets (segs_pos: %d, segs_len: %d)",
      //  rq_cnt, rq_.gso_segs_pos, rq_.gso_segs_len);
      
    }

    return batch_cnt;

  } else {
    ret = rte_eth_rx_burst(dpdk_port_id_, qid, (struct rte_mbuf **)pkts, cnt);
  }
#else
  ret = rte_eth_rx_burst(dpdk_port_id_, qid, (struct rte_mbuf **)pkts, cnt);
#endif

#if 0
  /* LOOM: DEBUG */
  {
    int i;
    for (i = 0; i < ret; i++) {
      //LOG(INFO) << "(Port " << name() << ") Packet Dump:" << pkts[i]->Dump();
      if (pkts[i]->nb_segs() > 1) {
        LOG(INFO) << "(Port " << name() << ") Number of segs: " << pkts[i]->nb_segs();
      }
      if (pkts[i]->total_len() > 1550) {
        LOG(INFO) << "(Port " << name() << ") Packet len > 1550 (" << pkts[i]->total_len() << ")";
      }
      if (!pkts[i]->is_linear()) {
        LOG(INFO) << "(Port " << name() << ") Packet is not linear!";
      }
      if (!pkts[i]->is_simple()) {
        LOG(INFO) << "(Port " << name() << ") Packet is not simple!";
      }
    }
  }
#endif

  return ret;
}

int PMDPort::SendPackets(queue_t qid, bess::Packet **pkts, int cnt) {
  /* LOOM: Configure TSO and checksumming offloads. */
  int i;
  for (i = 0; i < cnt; i++) {
    /* Only configure segmentation for large segments */
    if (needs_tso_csum_ && pkts[i]->total_len() > 1514) {
      //LOG(INFO) << "(Port " << name() << ") total_len: " << pkts[i]->total_len() <<
      //  ", mtu: " << mtu_;
      /* XXX: should be mtu_, not 1500 */
      config_pkt_offloads(pkts[i], mtu_);
    }
  }

  /* LOOM: DPDK SoftNIC Test/Hack */
  /* NOTE: This has been moved to the LoomPortOut module for now. */
  //if (driver_ == "net_softnic") {
  //  pkt_metadata_set(pkts, cnt);
  //}

#if 0
  /* LOOM: DEBUG */
  {
    int di;
    for (di = 0; di < cnt; di++) {
      if (pkts[di]->total_len() > 1514) {
        struct rte_mbuf *m = &pkts[di]->as_rte_mbuf();
        LOG(INFO) << "(Port " << name() << ") ol_flags: << " << //std::hex << 
          m->ol_flags << ", l2_len: " << m->l2_len << ", l3_len: " << 
          m->l3_len << ", l4_len: " << m->l4_len << ", tso_segsz: " <<
          m->tso_segsz;
      }

      //LOG(INFO) << "(Port " << name() << ") Packet Dump:" << pkts[di]->Dump();
      if (pkts[di]->nb_segs() > 1) {
        LOG(INFO) << "(Port " << name() << ") Number of segs: " << pkts[di]->nb_segs();
      }
      if (pkts[di]->total_len() > 1550) {
        LOG(INFO) << "(Port " << name() << ") Packet len > 1550 (" << pkts[di]->total_len() << ")";
      }
      if (!pkts[di]->is_linear()) {
        LOG(INFO) << "(Port " << name() << ") Packet is not linear!";
      }
      if (!pkts[di]->is_simple()) {
        LOG(INFO) << "(Port " << name() << ") Packet is not simple!";
      }

    }
  }
#endif

  int sent =
      rte_eth_tx_burst(dpdk_port_id_, qid, (struct rte_mbuf **)pkts, cnt);

  queue_stats[PACKET_DIR_OUT][qid].dropped += (cnt - sent);

  return sent;
}

Port::LinkStatus PMDPort::GetLinkStatus() {
  struct rte_eth_link status;
  // rte_eth_link_get() may block up to 9 seconds, so use _nowait() variant.
  rte_eth_link_get_nowait(dpdk_port_id_, &status);

  return LinkStatus{.speed = status.link_speed,
                    .full_duplex = static_cast<bool>(status.link_duplex),
                    .autoneg = static_cast<bool>(status.link_autoneg),
                    .link_up = static_cast<bool>(status.link_status)};
}

ADD_DRIVER(PMDPort, "pmd_port", "DPDK poll mode driver")
