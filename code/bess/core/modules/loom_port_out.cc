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

#include "loom_port_out.h"
#include "../utils/format.h"
#include "../drivers/pmd.h"

/* Includes for DPDK SoftNIC */
#include <rte_flow.h>
#include <rte_meter.h>
#include <rte_eth_softnic.h>
#include <rte_tm.h>

/* Includes for scheduling metadata */
/* TODO: Move into its own driver. */
#include "../metadata.h"
#include "../module.h"

CommandResponse LoomPortOut::Init(const bess::pb::LoomPortOutArg &arg) {
  const char *port_name;
  int ret;
  CommandResponse err;
  placement_constraint placement;

  if (!arg.port().length()) {
    return CommandFailure(EINVAL, "'port' must be given as a string");
  }
  port_name = arg.port().c_str();

  const auto &it = PortBuilder::all_ports().find(port_name);
  if (it == PortBuilder::all_ports().end()) {
    return CommandFailure(ENODEV, "Port %s not found", port_name);
  }
  port_ = it->second;

  if (port_->num_queues[PACKET_DIR_OUT] == 0) {
    return CommandFailure(ENODEV, "Port %s has no outgoing queue", port_name);
  }

  placement = port_->GetNodePlacementConstraint();
  node_constraints_ = placement;

  task_id_t tid = RegisterTask((void *)(uintptr_t)0);
  if (tid == INVALID_TASK_ID) {
    return CommandFailure(ENOMEM, "Task creation failed");
  }

  ret = port_->AcquireQueues(reinterpret_cast<const module *>(this),
                             PACKET_DIR_OUT, nullptr, 0);
  if (ret < 0) {
    return CommandFailure(-ret);
  }

  /* Add the metadata attrs for scheduling */
  using AccessMode = bess::metadata::Attribute::AccessMode;
  AddMetadataAttr("priority", 1, AccessMode::kRead);

  return CommandSuccess();
}

void LoomPortOut::DeInit() {
  if (port_) {
    port_->ReleaseQueues(reinterpret_cast<const module *>(this), PACKET_DIR_OUT,
                         nullptr, 0);
  }
}

std::string LoomPortOut::GetDesc() const {
  return bess::utils::Format("%s/%s", port_->name().c_str(),
                             port_->port_builder()->class_name().c_str());
}

/* LOOM: This task is used to pump the softNIC scheduler */
struct task_result LoomPortOut::RunTask(void *arg) {
  /* LOOM: TODO: This task should not have chilren, so this does not seem
   * relevant here. */
  //if (children_overload_ > 0) {
  //  return {
  //    .block = true,
  //    .packets = 0,
  //    .bits = 0,
  //  };
  //}

  Port *p = port_;
  PMDPort *pmdp = static_cast<PMDPort *>(p);
  /* LOOM: TODO: verify that the PMDPort uses the driver "net_softnic" */
  dpdk_port_t pmdp_id = pmdp->GetDpdkPortId();

  /* Arg is unused. */
  arg = arg;

  /* Pump the DPDK SoftNIC port. */
  /* LOOM: TODO: calling this on a non-softnic port will likely cause big
   * memory access related problems. */
  rte_pmd_softnic_run(pmdp_id);

  /* LOOM: TODO: This module should not have any children task.  I'm unsure if
   * RunNextModule(...) needs to be called anyways. */

  /* LOOM: TODO: rethink the return value here. */
  return {
    .block = false,
    .packets = 0,
    .bits = 0,
  };
}

#define RTE_SCHED_PORT_HIERARCHY(subport, pipe,           \
	traffic_class, queue, color)                          \
	((((uint64_t) (queue)) & 0x3) |                       \
	((((uint64_t) (traffic_class)) & 0x3) << 2) |         \
	((((uint64_t) (color)) & 0x3) << 4) |                 \
	((((uint64_t) (subport)) & 0xFFFF) << 16) |           \
	((((uint64_t) (pipe)) & 0xFFFFFFFF) << 32))

void LoomPortOut::PktMetadataSet(bess::PacketBatch *batch) {
  bess::Packet **pkts;
  uint32_t n_pkts;
  uint32_t i;

  pkts = batch->pkts();
  n_pkts = batch->cnt();

  for (i = 0; i < n_pkts; i++)	{
          bess::Packet *pkt = pkts[i];
          struct rte_mbuf *rte_pkt = &pkt->as_rte_mbuf();
          uint8_t priority = get_attr<uint8_t>(this, 0, pkt);

          /* LOOM: DEBUG. */
          //LOG(INFO) << bess::utils::Format("Packet priority: %d", priority);

          /* TODO: Error/sanity check priority. */

          uint64_t pkt_sched = RTE_SCHED_PORT_HIERARCHY(0, /* subport */
                                          0, /* pipe */
                                          priority, /* traffic class */
                                          0, /* queue */
                                          0); /* color */

          rte_pkt->hash.sched.lo = pkt_sched & 0xFFFFFFFF;
          rte_pkt->hash.sched.hi = pkt_sched >> 32;
  }
}

void LoomPortOut::ProcessBatch(bess::PacketBatch *batch) {
  Port *p = port_;

  const queue_t qid = get_igate();

  uint64_t sent_bytes = 0;
  int sent_pkts = 0;

  /* LOOM: Set DPDK SoftNIC scheduling metadata. */
  PktMetadataSet(batch);

  /* LOOM: TODO: preparing pkts for net_ixgbe offloads should probably happen
   * here and not in PMDPort. */

  if (likely(qid < port_->num_queues[PACKET_DIR_OUT])) {
    sent_pkts = p->SendPackets(qid, batch->pkts(), batch->cnt());
  }

  if (!(p->GetFlags() & DRIVER_FLAG_SELF_OUT_STATS)) {
    const packet_dir_t dir = PACKET_DIR_OUT;

    for (int i = 0; i < sent_pkts; i++) {
      sent_bytes += batch->pkts()[i]->total_len();
    }

    p->queue_stats[dir][qid].packets += sent_pkts;
    p->queue_stats[dir][qid].dropped += (batch->cnt() - sent_pkts);
    p->queue_stats[dir][qid].bytes += sent_bytes;
  }

  if (sent_pkts < batch->cnt()) {
    bess::Packet::Free(batch->pkts() + sent_pkts, batch->cnt() - sent_pkts);
  }
}

ADD_MODULE(LoomPortOut, "loom_port_out", "sends packets to a DPDK SoftNIC TM port")
