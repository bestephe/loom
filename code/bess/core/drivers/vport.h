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

#ifndef BESS_DRIVERS_VPORT_H_
#define BESS_DRIVERS_VPORT_H_

#include "../kmod/sn_common.h"
#include "../port.h"

static_assert(SN_MAX_TX_CTRLQ <= MAX_QUEUES_PER_DIR,
        "Cannot have more ctrl queues than max queues");
static_assert(SN_MAX_RXQ <= MAX_QUEUES_PER_DIR,
        "Cannot have more rxqs queues than max queues");
static_assert(sizeof(struct sn_tx_ctrl_desc) % sizeof(llring_addr_t) == 0,
	"Tx Ctrl desc must be a multiple of the pointer size");

class VPort final : public Port {
 public:
  VPort() : fd_(), bar_(), map_(), netns_fd_(), container_pid_(), use_tx_dataq_(), num_tx_dataqs_() {}
  void InitDriver() override;

  CommandResponse Init(const bess::pb::VPortArg &arg);
  void DeInit() override;

  int RecvPackets(queue_t qid, bess::Packet **pkts, int max_cnt) override;
  int SendPackets(queue_t qid, bess::Packet **pkts, int cnt) override;

 private:
  /* Loom. Currently used to save TSO state. Maybe more in the future. */
  /* Loom: This code is not used at the current moment. */
  struct txq_private {
    /* Current batch of segments. */
    int seg_cnt;
    int cur_seg;
    bess::Packet *segs[bess::PacketBatch::kMaxBurst];
    /* TODO: bess::Segment instead of bess::Packet. */

    /* Pool of packets to allocate segments to. */
    /* TODO: delete this? */
    struct llring* segpktpool;

    /* TSO state. */
    //uint16_t ip_offset;
    //uint16_t tcp_offset;
    //uint16_t tcp_hdrlen;
    uint16_t payload_offset;
    uint32_t seqoffset;
  };

  struct queue {
    union {
      struct sn_rxq_registers *rx_regs;
      struct sn_tx_ctrlq_registers *tx_regs;
    };

    struct llring *drv_to_sn;
    struct llring *sn_to_drv;
  };

  struct tx_data_queue {
    /* Loom. Used for TSO (Not used right now). Probably in the wrong place? */
    struct txq_private txq_priv;

    /* TODO: These should go somewhere else once a more general scheduling
     * algorithm is implemented. */
    bool active;
    uint64_t drr_deficit;
    bess::Packet* next_packet;

    struct llring *drv_to_sn;
  };

  struct dataq_drr {
    /* XXX: Just store the scheduling data in a data structure we can get from
     * the dataq index. */
    //CuckooMap<uint32_t, struct tx_data_queue*> active_dataqs;
    struct llring *dataq_ring;
    struct tx_data_queue *current_dataq;
    uint32_t quantum;
  };

  void FreeBar();
  void *AllocBar(struct tx_queue_opts *txq_opts,
                 struct rx_queue_opts *rxq_opts);
  int SetIPAddrSingle(const std::string &ip_addr);
  CommandResponse SetIPAddr(const bess::pb::VPortArg &arg);

  /* Loom: Note: The aren't used any more */
  int RefillSegs(queue_t qid, bess::Packet **segs, int max_cnt);
  bess::Packet *SegPkt(queue_t qid);

  /* Loom: TODO: use "struct queue *". This could be cleaner. */
  int DequeueCtrlDescs(struct queue *tx_ctrl_queue,
                       struct sn_tx_ctrl_desc *ctrl_desc_arr,
                       int max_cnt);
  int ProcessCtrlDescs(struct sn_tx_ctrl_desc *ctrl_desc_arr,
                       int cnt);
  /*
    allocates llring queue space and adds the queue to the specified flow with
    size indicated by slots. Takes the number of slots for the queue to have
    and the integer pointer to set on error.  Returns a llring queue.
  */
  /* Loom: This is a potentially misleading name. */
  llring* AddQueue(uint32_t slots, int* err);

  /* Dataq and scheduling functions. */
  int InitSchedState();
  int DeInitSchedState();

  /* Functions for DataQ DRR here. */
  int GetNextBatch(bess::Packet **pkts, int max_cnt);
  struct tx_data_queue* GetNextDrrDataq();
  int GetNextPackets(bess::Packet **pkts, int max_cnt,
                     struct tx_data_queue *dataq);
  bess::Packet* DataqReadPacket(struct tx_data_queue *dataq);

  /* Using data queues is optional */
  int RecvPacketsOld(queue_t qid, bess::Packet **pkts, int max_cnt);
  int RecvPacketsDataQ(queue_t qid, bess::Packet **pkts, int max_cnt);

  int fd_;

  char ifname_[IFNAMSIZ]; /* could be different from Name() */
  void *bar_;

  struct queue inc_ctrl_qs_[MAX_QUEUES_PER_DIR];
  struct queue out_qs_[MAX_QUEUES_PER_DIR];
  struct tx_data_queue inc_data_qs_[SN_MAX_TX_DATAQ];

  struct sn_ioc_queue_mapping map_;

  int netns_fd_;
  int container_pid_;

  bool use_tx_dataq_;
  int num_tx_dataqs_;

  /* Loom: scheduling state for deciding which dataQs to pull from. */
  struct dataq_drr dataq_drr_;
};

#endif  // BESS_DRIVERS_VPORT_H_
