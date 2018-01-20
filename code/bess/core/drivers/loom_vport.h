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

#ifndef BESS_DRIVERS_LOOMVPORT_H_
#define BESS_DRIVERS_LOOMVPORT_H_

#include "../kmod/sn_common.h"
#include "../port.h"

/* For PIFO scheduling. */
#include "../utils/pifo/pifo_pipeline_stage.h"
#include "../utils/pifo/pifo_pipeline.h"

static_assert(SN_MAX_TX_CTRLQ <= MAX_QUEUES_PER_DIR,
        "Cannot have more ctrl queues than max queues");
static_assert(SN_MAX_RXQ <= MAX_QUEUES_PER_DIR,
        "Cannot have more rxqs queues than max queues");
static_assert(sizeof(struct sn_tx_ctrl_desc) % sizeof(llring_addr_t) == 0,
	"Tx Ctrl desc must be a multiple of the pointer size");

/* Loom: Upper bound on the number of traffic classes. */
/* Loom: TODO: Small for now. */
#define SN_MAX_TC   (16)
#define SN_MAX_TENANT (16)

/* Different scheduling hierarchies currently supported. */
/* Loom: TODO: Make more general.  Ideally this would be able to be
 * automatically compiled with the (broken) pifo-compiler.py.  This is a long
 * ways off now though. */
enum SchHier {
  SCH_DRR,
  SCH_FIFO,
  SCH_2TEN_PRI,
  SCH_2TEN_FAIR,
  SCH_MTEN_PRIFAIR,
};

class LoomVPort final : public Port {
 public:
  LoomVPort() : fd_(), bar_(), map_(), netns_fd_(), container_pid_(), num_tx_ctrlqs_(),
    num_tx_dataqs_(), dataq_drr_(), pifo_state_() {}
  void InitDriver() override;

  CommandResponse Init(const bess::pb::LoomVPortArg &arg);
  void DeInit() override;

  int RecvPackets(queue_t qid, bess::Packet **pkts, int max_cnt) override;
  int SendPackets(queue_t qid, bess::Packet **pkts, int cnt) override;

  int RecvCtrlDesc();

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
    int dataq_num;

    /* Metadata for PIFO scheduling. */
    /* TODO: These should go somewhere else. */
    bool active;
    PIFOPacket pifo_entry;
    bess::Packet* next_packet;
    uint64_t next_xmit_ts;
    uint64_t next_tc;

    /* DataQ DRR state. */
    /* TODO: These should be deleted once a more general scheduling algorithm
     * is implemented. */
    uint64_t drr_deficit;
    struct llring *drv_to_sn;
  };

  struct pifo_pipeline_state {
    /* TODO: multiple stages. */
    PIFOPipeline *mesh;
    std::vector<PIFOArguments> tc_to_pifoargs[SN_MAX_TC];
    std::vector<std::pair<uint64_t, uint64_t>> tc_to_sattrs[SN_MAX_TC]; /* TC -> static attributes of the class. */
    /* Loom: XXX: TODO: this would be better as a map from the unique node id
     * in the tree to the generic state for the tree. */
    uint64_t root_vt; /* l0_vt */
    uint64_t l1_vt[SN_MAX_TENANT]; /* "tenant" */
    uint64_t l2_vt[SN_MAX_TC]; /* "tc" */
    int tick;

    //pifo_pipeline_state() : {};
    //~pifo_pipeline_state() {};
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
  CommandResponse SetIPAddr(const bess::pb::LoomVPortArg &arg);

  /* Loom: Note: The aren't used any more */
  int RefillSegs(queue_t qid, bess::Packet **segs, int max_cnt);
  bess::Packet *SegPkt(queue_t qid);

  /* Loom: TODO: use "struct queue *". This could be cleaner. */
  int DequeueCtrlDescs(struct queue *tx_ctrl_queue,
                       struct sn_tx_ctrl_desc *ctrl_desc_arr,
                       int max_cnt);
  int ProcessCtrlDescs(struct sn_tx_ctrl_desc *ctrl_desc_arr,
                       int cnt);
  bess::Packet* DataqReadPacket(struct tx_data_queue *dataq);

  /* Dataq and scheduling init/deint functions. */
  int InitSchedState();
  int DeInitSchedState();

  /* Functions for DataQ PIFO scheduling here. */
  /* Init needs to be made generic. */
  int InitPifoMeshFifo();
  int InitPifoMesh2TenantPrio();
  int InitPifoMesh2TenantFair();
  int InitPifoMeshMTenantPriFair();
  int InitPifoState();
  int DeInitPifoState();
  int AddNewPifoDataq(struct sn_tx_ctrl_desc *ctrl_desc);
  int AddDataqToPifo(struct tx_data_queue *dataq);
  int GetNextPifoBatch(bess::Packet **pkts, int max_cnt);
  struct tx_data_queue* GetNextPifoDataq();
  int GetNextPifoPackets(bess::Packet **pkts, int max_cnt,
                         struct tx_data_queue *dataq,
                         uint64_t *total_bytes);

  /* Functions for DataQ DRR here. */
  int InitDrrState();
  int DeInitDrrState();
  int AddNewDrrDataq(struct sn_tx_ctrl_desc *ctrl_desc);
  /* Loom: This is a potentially misleading name. */
  llring* AddDrrQueue(uint32_t slots, int* err);
  int GetNextDrrBatch(bess::Packet **pkts, int max_cnt);
  struct tx_data_queue* GetNextDrrDataq();
  int GetNextDrrPackets(bess::Packet **pkts, int max_cnt,
                     struct tx_data_queue *dataq);


  /* Using data queues was optional */
  int RecvPacketsDataQ(queue_t qid, bess::Packet **pkts, int max_cnt);

  int fd_;

  char ifname_[IFNAMSIZ]; /* could be different from Name() */
  void *bar_;

  /* inc_qs_ are entirely different than both the control and data queues used
   * by the driver to talk to sn (loom_vport).  inc_qs_ in this case is the
   * number of queues exposed to the BESS scheduler (through LoomPortInc).
   * This is mostly useful for allowing for QOS through configuring the BESS
   * scheduler. */
  struct queue inc_qs_[MAX_QUEUES_PER_DIR];
  struct queue out_qs_[MAX_QUEUES_PER_DIR];

  struct queue inc_ctrl_qs_[MAX_QUEUES_PER_DIR];
  struct tx_data_queue inc_data_qs_[SN_MAX_TX_DATAQ];

  struct sn_ioc_queue_mapping map_;

  int netns_fd_;
  int container_pid_;

  int num_tx_ctrlqs_;
  int num_tx_dataqs_;

  /* Loom: TODO: Replace with PIFOs */
  struct dataq_drr dataq_drr_;

  /* Loom: scheduling state for deciding which dataQs to pull from. */
  enum SchHier sch_hier_;
  struct pifo_pipeline_state pifo_state_;
};

#endif  // BESS_DRIVERS_LOOMVPORT_H_
