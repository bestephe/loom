#ifndef BESS_DRIVERS_VPORT_H_
#define BESS_DRIVERS_VPORT_H_

#include "../kmod/sn_common.h"
#include "../port.h"

class VPort final : public Port {
 public:
  VPort() : fd_(), bar_(), map_(), netns_fd_(), container_pid_() {}
  void InitDriver() override;

  CommandResponse Init(const bess::pb::VPortArg &arg);
  void DeInit() override;

  int RecvPackets(queue_t qid, bess::Packet **pkts, int max_cnt) override;
  int SendPackets(queue_t qid, bess::Packet **pkts, int cnt) override;

 private:
  /* Loom. Currently used to save TSO state. Maybe more in the future. */
  struct txq_private {
    /* Current batch of segments. */
    int seg_cnt;
    int cur_seg;
    bess::Packet *segs[bess::PacketBatch::kMaxBurst];
    /* TODO: bess::Segment instead of bess::Packet. */

    /* Pool of packets to allocate segments to. */
    struct llring* segpktpool;

    /* TSO state. */
    uint16_t ip_offset;
    uint16_t tcp_offset;
    uint16_t tcp_hdrlen;
    uint16_t payload_offset;
    uint32_t seq;
  };

  struct queue {
    union {
      struct sn_rxq_registers *rx_regs;
    };

    /* Loom. Probably in the wrong place? */
    struct txq_private txq_priv;

    struct llring *drv_to_sn;
    struct llring *sn_to_drv;
  };

  void FreeBar();
  void *AllocBar(struct tx_queue_opts *txq_opts,
                 struct rx_queue_opts *rxq_opts);
  int SetIPAddrSingle(const std::string &ip_addr);
  CommandResponse SetIPAddr(const bess::pb::VPortArg &arg);

  /* LOOM: Apologies for putting things in the wrong places. */
  int RefillSegs(queue_t qid, bess::Packet **segs, int max_cnt);
  bess::Packet *SegPkt(queue_t qid);

  int fd_;

  char ifname_[IFNAMSIZ]; /* could be different from Name() */
  void *bar_;

  struct queue inc_qs_[MAX_QUEUES_PER_DIR];
  struct queue out_qs_[MAX_QUEUES_PER_DIR];

  struct sn_ioc_queue_mapping map_;

  int netns_fd_;
  int container_pid_;
};

#endif  // BESS_DRIVERS_VPORT_H_
