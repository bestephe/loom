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

#include "loom_vport.h"

#include <fcntl.h>
#include <libgen.h>
#include <sched.h>
#include <unistd.h>

#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include <rte_config.h>
#include <rte_malloc.h>
#include <rte_ether.h>
#include <rte_ip.h>
#include <rte_tcp.h>

#include "../message.h"
#include "../utils/format.h"

#include "utils/checksum.h"
#include "utils/ether.h"
#include "utils/format.h"
#include "utils/ip.h"
//#include "utils/tcp.h"
#include "utils/udp.h"

using bess::utils::Ethernet;
using bess::utils::Ipv4;
using bess::utils::Udp;
using bess::utils::Tcp;
using bess::utils::be16_t;
using bess::utils::be32_t;

/* LOOM: TODO: this include and using the asserts it defines is probably not
 * best practice coding style for this application. */
//#include <gtest/gtest.h>
#include <cassert>

/* LOOM: Used for segmentation. */
#define FRAME_SIZE	1514		/* TODO: check pport MTU */
#define FQ_DEFICIT      (100 * 48000)

/* TODO: Unify vport and vport_native */

#define SLOTS_PER_LLRING 1024

#define REFILL_LOW 64
/* TODO: LOOM: Should this be bigger for segmentation offloading? */
#define REFILL_HIGH 512

/* This watermark is to detect congestion and cache bouncing due to
 * head-eating-tail (needs at least 8 slots less then the total ring slots).
 * Not sure how to tune this... */
#define SLOTS_WATERMARK ((SLOTS_PER_LLRING >> 3) * 7) /* 87.5% */

/* Disable (0) single producer/consumer mode for default */
#define SINGLE_P 0
#define SINGLE_C 0

#define ROUND_TO_64(x) ((x + 32) & (~0x3f))

/* Loom: TODO: Make more general. */
#define DRR_QUANTUM (16834)

static inline int find_next_nonworker_cpu(int cpu) {
  do {
    cpu = (cpu + 1) % sysconf(_SC_NPROCESSORS_ONLN);
  } while (is_worker_core(cpu));
  return cpu;
}

static int refill_tx_bufs(struct llring *r) {
  bess::Packet *pkts[REFILL_HIGH];
  phys_addr_t objs[REFILL_HIGH];

  int deficit;
  int cnt;
  int ret;

  int curr_cnt = llring_count(r);

  /* LOOM: Avoid tx hangs? */
  //if (curr_cnt >= REFILL_LOW)
  //  return 0;

  deficit = REFILL_HIGH - curr_cnt;

  cnt = bess::Packet::Alloc((bess::Packet **)pkts, deficit, 0);
  if (cnt == 0)
    return 0;

  for (int i = 0; i < cnt; i++)
    objs[i] = pkts[i]->paddr();

  ret = llring_mp_enqueue_bulk(r, objs, cnt);
  DCHECK_EQ(ret, 0);

  return cnt;
}

/* LOOM: UGLY.  I didn't want to uncessarily pay for getting a packet from its
 * paddr though. */
static void refill_segpktpool(struct llring *r) {
  bess::Packet *pkts[REFILL_HIGH];
  //phys_addr_t objs[REFILL_HIGH];

  int deficit;
  int ret;

  int curr_cnt = llring_count(r);

  if (curr_cnt >= REFILL_LOW)
    return;

  deficit = REFILL_HIGH - curr_cnt;

  ret = bess::Packet::Alloc((bess::Packet **)pkts, deficit, 0);
  if (ret == 0)
    return;

  //for (int i = 0; i < ret; i++) {
  //  /* LOOM: This next line is pkts[i], not pkts[i]->paddr().  This is the only
  //   * difference between refill_segpktpool and refill_tx_bufs. */
  //  objs[i] = reinterpret_cast<phys_addr_t >(pkts[i]);
  //}

  //ret = llring_mp_enqueue_bulk(r, objs, ret);
  ret = llring_mp_enqueue_bulk(r, reinterpret_cast<llring_addr_t*>(pkts), ret);
  DCHECK_EQ(ret, 0);
}

static void drain_sn_to_drv_q(struct llring *q) {
  /* sn_to_drv queues contain physical address of packet buffers */
  for (;;) {
    phys_addr_t paddr;
    bess::Packet *snb;
    int ret;

    ret = llring_mc_dequeue(q, &paddr);
    if (ret)
      break;

    snb = bess::Packet::from_paddr(paddr);
    if (!snb) {
      LOG(ERROR) << "from_paddr(" << paddr << ") failed";
      continue;
    }

    bess::Packet::Free(snb);
  }
}

static void drain_drv_to_sn_q(struct llring *q) {
  /* sn_to_drv queues contain virtual address of packet buffers */
  for (;;) {
    phys_addr_t paddr;
    int ret;

    ret = llring_mc_dequeue(q, &paddr);
    if (ret)
      break;

    bess::Packet::Free(bess::Packet::from_paddr(paddr));
  }
}

static void drain_and_free_segpktpool(struct llring *q) {
  for (;;) {
    bess::Packet *pkt;
    int ret;

    /* TODO: Should this use the phys_addr_t aproach taken in SegPkt as well? */
    ret = llring_mc_dequeue(q, reinterpret_cast<llring_addr_t*>(&pkt));
    if (ret)
      break;

    bess::Packet::Free(pkt);
  }

  std::free(q);
}

static void reclaim_packets(struct llring *ring) {
  phys_addr_t objs[bess::PacketBatch::kMaxBurst];
  bess::Packet *pkts[bess::PacketBatch::kMaxBurst];
  int ret;

  for (;;) {
    ret = llring_mc_dequeue_burst(ring, objs, bess::PacketBatch::kMaxBurst);
    if (ret == 0)
      break;
    for (int i = 0; i < ret; i++) {
      pkts[i] = bess::Packet::from_paddr(objs[i]);
    }
    bess::Packet::Free(pkts, ret);
  }
}

static CommandResponse docker_container_pid(const std::string &cid,
                                            int *container_pid) {
  char buf[1024];

  FILE *fp;

  int ret;
  int exit_code;

  if (cid.length() == 0)
    return CommandFailure(EINVAL,
                          "field 'docker' should be "
                          "a containder ID or name in string");

  ret = snprintf(buf, static_cast<int>(sizeof(buf)),
                 "docker inspect --format '{{.State.Pid}}' "
                 "%s 2>&1",
                 cid.c_str());
  if (ret >= static_cast<int>(sizeof(buf)))
    return CommandFailure(EINVAL,
                          "The specified Docker "
                          "container ID or name is too long");

  fp = popen(buf, "r");
  if (!fp) {
    return CommandFailure(
        ESRCH, "Command 'docker' is not available. (not installed?)");
  }

  ret = fread(buf, 1, sizeof(buf) - 1, fp);
  if (ret == 0)
    return CommandFailure(ENOENT,
                          "Cannot find the PID of "
                          "container %s",
                          cid.c_str());

  buf[ret] = '\0';

  ret = pclose(fp);
  exit_code = WEXITSTATUS(ret);

  if (exit_code != 0 || sscanf(buf, "%d", container_pid) == 0) {
    return CommandFailure(ESRCH, "Cannot find the PID of container %s",
                          cid.c_str());
  }

  return CommandSuccess();
}

static int next_cpu;

/* Free an allocated bar, freeing resources in the queues */
void LoomVPort::FreeBar() {
  uint32_t i;
  struct sn_conf_space *conf = static_cast<struct sn_conf_space *>(bar_);

  for (i = 0; i < conf->num_tx_ctrlq; i++) {
    drain_drv_to_sn_q(inc_ctrl_qs_[i].drv_to_sn);
    drain_sn_to_drv_q(inc_ctrl_qs_[i].sn_to_drv);
  }

  for (i = 0; i < conf->num_tx_dataq; i++) {
    drain_drv_to_sn_q(inc_data_qs_[i].drv_to_sn);

    /* LOOM. Ugly. */
    /* TODO: Currently TSO is disabled GSO provides good enough performance, so
     * this code is not needed. */
    struct txq_private *txq_priv = &inc_data_qs_[i].txq_priv;
    while (txq_priv->cur_seg < txq_priv->seg_cnt) {
      bess::Packet::Free(txq_priv->segs[txq_priv->cur_seg]);
      txq_priv->segs[txq_priv->cur_seg] = nullptr;
      txq_priv->cur_seg++;
    }
    txq_priv->cur_seg = 0;
    txq_priv->seg_cnt = 0;

    drain_and_free_segpktpool(inc_data_qs_[i].txq_priv.segpktpool);
    inc_data_qs_[i].txq_priv.segpktpool = nullptr; /* Not necessary. */
  }

  for (i = 0; i < conf->num_rxq; i++) {
    /* Loom: Note: This line of code was wrong (inc_qs_) in the original code. */
    drain_drv_to_sn_q(out_qs_[i].drv_to_sn);
    drain_sn_to_drv_q(out_qs_[i].sn_to_drv);
  }


  rte_free(bar_);
}

void *LoomVPort::AllocBar(struct tx_queue_opts *txq_opts,
                      struct rx_queue_opts *rxq_opts) {
  int bytes_per_llring;
  int total_bytes;

  void *bar;
  struct sn_conf_space *conf;
  char *ptr;

  uint32_t i;

  /* Loom: TODO: from currently unused TSO implementation. */
  struct txq_private *txq_priv;

  /* Loom: TODO: Allow the number of scheduling queues to be configured. */
  assert(num_tx_dataqs_ <= SN_MAX_TX_DATAQ);

  bytes_per_llring = llring_bytes_with_slots(SLOTS_PER_LLRING);

  total_bytes = ROUND_TO_64(sizeof(struct sn_conf_space));
  total_bytes += num_tx_ctrlqs_ * 
                 (ROUND_TO_64(sizeof(struct sn_tx_ctrlq_registers)) +
                  2 * ROUND_TO_64(bytes_per_llring));
  total_bytes += num_queues[PACKET_DIR_OUT] *
                 (ROUND_TO_64(sizeof(struct sn_rxq_registers)) +
                  2 * ROUND_TO_64(bytes_per_llring));
  total_bytes += num_tx_dataqs_ * 
                 (ROUND_TO_64(bytes_per_llring));

  VLOG(1) << "BAR total_bytes = " << total_bytes;
  bar = rte_zmalloc(nullptr, total_bytes, 64);
  DCHECK(bar);

  conf = reinterpret_cast<struct sn_conf_space *>(bar);

  conf->bar_size = total_bytes;
  conf->netns_fd = netns_fd_;
  conf->container_pid = container_pid_;

  strncpy(conf->ifname, ifname_, IFNAMSIZ);

  bess::utils::Copy(conf->mac_addr, mac_addr, ETH_ALEN);

  conf->num_tx_ctrlq = num_tx_ctrlqs_;
  conf->num_tx_dataq = num_tx_dataqs_;
  conf->num_rxq = num_queues[PACKET_DIR_OUT];
  conf->link_on = 1;
  conf->promisc_on = 1;
  conf->dataq_on = 1;

  conf->txq_opts = *txq_opts;
  conf->rxq_opts = *rxq_opts;

  ptr = (char *)(conf);
  ptr += ROUND_TO_64(sizeof(struct sn_conf_space));

  /* See sn_common.h for the llring usage */

  for (i = 0; i < conf->num_tx_ctrlq; i++) {
    /* TX ctrl queue registers */
    inc_ctrl_qs_[i].tx_regs = reinterpret_cast<struct sn_tx_ctrlq_registers *>(ptr);
    ptr += ROUND_TO_64(sizeof(struct sn_tx_ctrlq_registers));

    /* Driver -> BESS */
    llring_init(reinterpret_cast<struct llring *>(ptr), SLOTS_PER_LLRING,
                SINGLE_P, SINGLE_C);
    inc_ctrl_qs_[i].drv_to_sn = reinterpret_cast<struct llring *>(ptr);
    ptr += ROUND_TO_64(bytes_per_llring);

    /* BESS -> Driver */
    llring_init(reinterpret_cast<struct llring *>(ptr), SLOTS_PER_LLRING,
                SINGLE_P, SINGLE_C);
    refill_tx_bufs(reinterpret_cast<struct llring *>(ptr));
    inc_ctrl_qs_[i].sn_to_drv = reinterpret_cast<struct llring *>(ptr);
    ptr += ROUND_TO_64(bytes_per_llring);
  }

  for (i = 0; i < conf->num_rxq; i++) {
    /* RX queue registers */
    out_qs_[i].rx_regs = reinterpret_cast<struct sn_rxq_registers *>(ptr);
    ptr += ROUND_TO_64(sizeof(struct sn_rxq_registers));

    /* Driver -> BESS */
    llring_init(reinterpret_cast<struct llring *>(ptr), SLOTS_PER_LLRING,
                SINGLE_P, SINGLE_C);
    out_qs_[i].drv_to_sn = reinterpret_cast<struct llring *>(ptr);
    ptr += ROUND_TO_64(bytes_per_llring);

    /* BESS -> Driver */
    llring_init(reinterpret_cast<struct llring *>(ptr), SLOTS_PER_LLRING,
                SINGLE_P, SINGLE_C);
    out_qs_[i].sn_to_drv = reinterpret_cast<struct llring *>(ptr);
    ptr += ROUND_TO_64(bytes_per_llring);
  }

  /* Loom: TODO: data queues could be smaller than ctrl queues if memory
   * becomes a problem. */
  for (i = 0; i < conf->num_tx_dataq; i++) {
    /* TX data queue Driver -> BESS */
    llring_init(reinterpret_cast<struct llring *>(ptr), SLOTS_PER_LLRING,
                SINGLE_P, SINGLE_C);
    inc_data_qs_[i].drv_to_sn = reinterpret_cast<struct llring *>(ptr);
    ptr += ROUND_TO_64(bytes_per_llring);
  }

  /* Loom: Initialize txq private data used for TSO. */
  /* TODO: Currently TSO is disabled GSO provides good enough performance (with
   * Linux 4.9), so this initialization code is not needed. */
  for (i = 0; i < conf->num_tx_dataq; i++) {
    /* LOOM: Initialize txq private data for TSO/Lookback/etc.
     *  This could be done in its own function, but this function would need to
     *  be a private member of the class as things are currently defined.  */

    inc_data_qs_[i].dataq_num = i;

    size_t ring_sz = 0;
    int ret;

    txq_priv = &inc_data_qs_[i].txq_priv;
    memset(txq_priv, 0, sizeof(*txq_priv));

    /* I'm not sure using an llring as a pool of packets is the best idea here.
     * */
    /* LOOM: TODO: I think I've given up on TSO support in the kmod.
     * segpktpool should probably be removed. */
    ring_sz = llring_bytes_with_slots(SLOTS_PER_LLRING);
    txq_priv->segpktpool = reinterpret_cast<struct llring*>(
        aligned_alloc(alignof(llring), ring_sz));
    CHECK(txq_priv->segpktpool);
    ret = llring_init(txq_priv->segpktpool, SLOTS_PER_LLRING, SINGLE_P, SINGLE_C);
    DCHECK_EQ(ret, 0);
    /* TODO: The seg pkt pool is not used right now. */
    //refill_segpktpool(txq_priv->segpktpool);
  }

  return bar;
}

void LoomVPort::InitDriver() {
  struct stat buf;

  int ret;

  next_cpu = 0;

  ret = stat("/dev/bess", &buf);
  if (ret < 0) {
    char exec_path[1024];
    char *exec_dir;

    char cmd[2048];

    LOG(INFO) << "loom_vport: BESS kernel module is not loaded. Loading...";

    ret = readlink("/proc/self/exe", exec_path, sizeof(exec_path));
    if (ret == -1 || ret >= static_cast<int>(sizeof(exec_path)))
      return;

    exec_path[ret] = '\0';
    exec_dir = dirname(exec_path);

    snprintf(cmd, sizeof(cmd), "insmod %s/kmod/bess.ko", exec_dir);
    ret = system(cmd);
    if (WEXITSTATUS(ret) != 0) {
      LOG(WARNING) << "Cannot load kernel module " << exec_dir
                   << "/kmod/bess.ko";
    }
  }
}

int LoomVPort::SetIPAddrSingle(const std::string &ip_addr) {
  FILE *fp;

  char buf[1024];

  int ret;
  int exit_code;

  ret = snprintf(buf, sizeof(buf), "ip addr add %s dev %s 2>&1",
                 ip_addr.c_str(), ifname_);
  if (ret >= static_cast<int>(sizeof(buf)))
    return -EINVAL;

  fp = popen(buf, "r");
  if (!fp)
    return -errno;

  ret = pclose(fp);
  exit_code = WEXITSTATUS(ret);
  if (exit_code)
    return -EINVAL;

  return 0;
}

CommandResponse LoomVPort::SetIPAddr(const bess::pb::LoomVPortArg &arg) {
  int child_pid = 0;

  int ret = 0;
  int nspace = 0;

  /* change network namespace if necessary */
  if (container_pid_ || netns_fd_ >= 0) {
    nspace = 1;

    child_pid = fork();
    if (child_pid < 0) {
      return CommandFailure(-child_pid);
    }

    if (child_pid == 0) {
      char buf[1024];
      int fd;

      if (container_pid_) {
        snprintf(buf, sizeof(buf), "/proc/%d/ns/net", container_pid_);
        fd = open(buf, O_RDONLY);
        if (fd < 0) {
          PLOG(ERROR) << "open(/proc/pid/ns/net)";
          _exit(errno <= 255 ? errno : ENOMSG);
        }
      } else
        fd = netns_fd_;

      ret = setns(fd, 0);
      if (ret < 0) {
        PLOG(ERROR) << "setns()";
        _exit(errno <= 255 ? errno : ENOMSG);
      }
    } else {
      goto wait_child;
    }
  }

  if (arg.ip_addrs_size() > 0) {
    for (int i = 0; i < arg.ip_addrs_size(); ++i) {
      const char *addr = arg.ip_addrs(i).c_str();
      ret = SetIPAddrSingle(addr);
      if (ret < 0) {
        if (nspace) {
          /* it must be the child */
          DCHECK_EQ(child_pid, 0);
          _exit(errno <= 255 ? errno : ENOMSG);
        } else
          break;
      }
    }
  } else {
    DCHECK(0);
  }

  if (nspace) {
    if (child_pid == 0) {
      if (ret < 0) {
        ret = -ret;
        _exit(ret <= 255 ? ret : ENOMSG);
      } else
        _exit(0);
    } else {
      int exit_status;

    wait_child:
      ret = waitpid(child_pid, &exit_status, 0);

      if (ret >= 0) {
        DCHECK_EQ(ret, child_pid);
        ret = -WEXITSTATUS(exit_status);
      } else
        PLOG(ERROR) << "waitpid()";
    }
  }

  if (ret < 0) {
    return CommandFailure(-ret,
                          "Failed to set IP addresses "
                          "(incorrect IP address format?)");
  }

  return CommandSuccess();
}

llring* LoomVPort::AddDrrQueue(uint32_t slots, int* err) {
  int bytes = llring_bytes_with_slots(slots);
  int ret;

  llring* q = static_cast<llring*>(aligned_alloc(alignof(llring), bytes));
  if (!q) {
    *err = -ENOMEM;
    return nullptr;
  }

  /* Loom: TODO: what should single_p and single_c be in this case? */
  ret = llring_init(q, slots, 0, 0);
  if (ret) {
    std::free(q);
    *err = -EINVAL;
    return nullptr;
  }
  return q;
}

int LoomVPort::InitSchedState() {
  int ret = 0;

  ret = InitPifoState();

  /* Loom: TODO: Remove the naive DRR across dataqs implementation */
  InitDrrState();

  return ret;
}

int LoomVPort::DeInitSchedState() {
  DeInitPifoState();

  /* Loom: TODO: Remove the naive DRR across dataqs implementation */
  DeInitDrrState();

  return (0);
}

int LoomVPort::InitPifoMeshFifo() {
  struct pifo_pipeline_state *pipe = &pifo_state_;

  /* Note(Brent): In this function, I have some concern about whether these
   * objects are copied or if I'm doing something bad by keeping references to
   * things allocated on the stack. */
  /* Note(Brent): I think I've convinced myself that all stack allocated
   * objects are deep copied into std::map and std::vector objects. */

  /* Loom: DEBUG */
  LOG(INFO) << "InitPifoMeshFifo";

  /* Init the PIFO stages. */
  PIFOPipelineStage pifo1(1, FIELD_PTR, {{0, {Operation::TRANSMIT, {}}},},
    [] (const auto & x) {return x(FIELD_XMIT_TS);});
  pipe->mesh = new PIFOPipeline({pifo1,});
  pipe->tick = 0;

  /* Create a mapping from priority 0 to the appropriate PIFOArguments. */
  std::vector<PIFOArguments> all_tenants = {{0, QueueType::PRIORITY_QUEUE, 0}};
  pipe->tc_to_pifoargs[0] = all_tenants;
  /* Loom: TODO: memset other pifoargs. */

  /* Set the static state and virtual time state. */
  pipe->tc_to_sattrs[0] = {{FIELD_TENANT, 0}, {FIELD_TC, 0},};
  pipe->l1_vt[0] = 0;
  pipe->l2_vt[0] = 0;

  return (0);
}

int LoomVPort::InitPifoMesh2TenantPrio() {
  struct pifo_pipeline_state *pipe = &pifo_state_;

  /* Note(Brent): In this function, I have some concern about whether these
   * objects are copied or if I'm doing something bad by keeping references to
   * things allocated on the stack. */
  /* Note(Brent): I think I've convinced myself that all stack allocated
   * objects are deep copied into std::map and std::vector objects. */

  /* Loom: DEBUG */
  LOG(INFO) << "InitPifoMesh2TenantPrio";

  /* Init the PIFO stages. */
  PIFOArguments targetTenant2={1,QueueType::PRIORITY_QUEUE,1};
  PIFOArguments targetTenant1={1,QueueType::PRIORITY_QUEUE,0};

  /* Strict tenant priority at the root. */
  PIFOPipelineStage pifo1(1,
                          FIELD_TC,
                          {{3, {Operation::DEQ,{targetTenant2}}},
                           {0, {Operation::DEQ,{targetTenant1}}},
                          },
                          [] (const auto & x) {
                            /* Loom: Hack because tc 3 is high_prio in the exps. */
                            if (x(FIELD_TC) == 3) {
                                return (0ul);
                            } else if (x(FIELD_TC) == 0) {
                                return (3ul);
                            } else {
                                return x(FIELD_TC);
                            }
                          });
  /* FIFO for each tenant. */
  PIFOPipelineStage pifo2(2,
                          FIELD_PTR,
                          {{0, {Operation::TRANSMIT, {}}}},
                          [] (const auto & x) {
                            return x(FIELD_XMIT_TS);
                          });
  pipe->mesh = new PIFOPipeline({pifo1,pifo2,});

  /* Create a mapping from priority 0 to the appropriate PIFOArguments. */
  PIFOArguments root_pifo_args = {0, QueueType::PRIORITY_QUEUE, 0};
  pipe->tc_to_pifoargs[0] = {root_pifo_args, targetTenant1};
  pipe->tc_to_pifoargs[3] = {root_pifo_args, targetTenant2};
  /* Loom: TODO: memset other pifoargs. */

  /* Set the static state and virtual time state. */
  pipe->tc_to_sattrs[0] = {{FIELD_TENANT, 0}, {FIELD_TC, 0}};
  pipe->tc_to_sattrs[3] = {{FIELD_TENANT, 3}, {FIELD_TC, 3}};
  pipe->l1_vt[0] = 0; pipe->l1_vt[3] = 0;
  pipe->l2_vt[0] = 0; pipe->l2_vt[3] = 0;

  return (0);
}

int LoomVPort::InitPifoMesh2TenantFair() {
  struct pifo_pipeline_state *pipe = &pifo_state_;

  /* Note(Brent): In this function, I have some concern about whether these
   * objects are copied or if I'm doing something bad by keeping references to
   * things allocated on the stack. */
  /* Note(Brent): I think I've convinced myself that all stack allocated
   * objects are deep copied into std::map and std::vector objects. */

  /* Loom: DEBUG */
  LOG(INFO) << "InitPifoMesh2TenantFair";

  /* Init the PIFO stages. */
  PIFOArguments targetTenant2={1,QueueType::PRIORITY_QUEUE,1};
  PIFOArguments targetTenant1={1,QueueType::PRIORITY_QUEUE,0};

  /* Strict tenant priority at the root. */
  PIFOPipelineStage pifo1(1,
                          FIELD_TC,
                          {{3, {Operation::DEQ,{targetTenant2}}},
                           {0, {Operation::DEQ,{targetTenant1}}},
                           //The following autogenerated line seems wrong. 
                           //{2, {Operation::DEQ,targetTenant1}},
                          },
                          [this] (const auto & x) {
                            static std::map<uint64_t, uint64_t> last_fin_time = {{0, 0}, {3, 0}};
                            uint64_t tc = x(FIELD_TC);
                            //uint64_t root_vt = x(FIELD_ROOT_VT);
                            uint64_t root_vt = pifo_state_.root_vt;
                            uint64_t fin_time = std::max(last_fin_time[tc], root_vt);

                            /* This is bad and could lead to packet bursts from
                             * all of the dataq's in the same traffic class
                             * because we don't know how many bytes will be
                             * sent until after we check the queue. */
                            /* Another way of saying this: we're only doing
                             * per-batch fair queuing, not per-byte fair
                             * queueing */
                            //fin_time += 1; 
                            fin_time += (x(FIELD_UNACCNT_BYTES) + 1);

                            /* Workaround to save the virtual time used for
                             * this dataq despite it being constant. */ 
                            /* Loom: Note: TODO: The PIFO design seems
                             * fundamentally broken/limiting right now.  There
                             * should also be a lambda for saving shared state
                             * on dequeue operations. */
                            inc_data_qs_[x(FIELD_DATAQ_NUM)].pifo_entry(FIELD_ROOT_VT) = fin_time;
                            //x(FIELD_ROOT_VT) = fin_time;
                            last_fin_time.at(tc) = fin_time;

                            return fin_time;
                          });
  /* FIFO for each tenant. */
  PIFOPipelineStage pifo2(2,
                          FIELD_PTR,
                          {{0, {Operation::TRANSMIT, {}}}},
                          [] (const auto & x) {
                            return x(FIELD_XMIT_TS);
                          });
  pipe->mesh = new PIFOPipeline({pifo1,pifo2,});

  /* Create a mapping from priority 0 to the appropriate PIFOArguments. */
  PIFOArguments root_pifo_args = {0, QueueType::PRIORITY_QUEUE, 0};
  pipe->tc_to_pifoargs[0] = {root_pifo_args, targetTenant1};
  pipe->tc_to_pifoargs[3] = {root_pifo_args, targetTenant2};
  /* Loom: TODO: memset other pifoargs. */

  /* Set the static state and virtual time state. */
  pipe->tc_to_sattrs[0] = {{FIELD_TENANT, 0}, {FIELD_TC, 0}};
  pipe->tc_to_sattrs[3] = {{FIELD_TENANT, 3}, {FIELD_TC, 3}};
  pipe->l1_vt[0] = 0; pipe->l1_vt[3] = 0;
  pipe->l2_vt[0] = 0; pipe->l2_vt[3] = 0;

  return (0);
}

/* Overview of the scheduling algorithm:
 * - There are 8 total tenants
 *   - Different tenants fairly share available bandwidth
 * - Each tenant has two traffic classes (t<<1, (t<<1)+1):
 *   - E.g., Tenant 0 has tcs {0, 1}, Tenant 1 has tcs {2, 3}, ...
 *   - TC t has strict priority over traffic class t+1 
 *   - TC t uses FIFO scheduling
 *   - TC t+1 uses per-dataq fair queuing (FQ)
 */
int LoomVPort::InitPifoMeshMTenantPriFair() {
  struct pifo_pipeline_state *pipe = &pifo_state_;
  uint64_t num_tenants = 8;
  uint64_t num_tcs = num_tenants << 1;

  assert(num_tenants <= SN_MAX_TENANT);
  assert(num_tcs <= SN_MAX_TC);

  /* Loom: DEBUG */
  LOG(INFO) << "InitPifoMeshMTenantPriFair";

  /* Init the PIFO arguments for each stage. */
  PIFOArguments root_pifo_args = {0, QueueType::PRIORITY_QUEUE, 0};
  std::map<uint64_t, PIFOArguments> tenantArguments;
  std::vector<std::pair<const int, NextHop>> tenantLut;
  std::map<uint64_t, PIFOArguments> tcArguments;
  std::vector<std::pair<const int, NextHop>> tcLut;
  pipe->root_vt = 0;
  for (uint64_t tenant = 0; tenant < num_tenants; tenant++) {
    tenantArguments[tenant] = PIFOArguments({1, QueueType::PRIORITY_QUEUE, tenant});
    tenantLut.push_back({tenant, {Operation::DEQ, {tenantArguments[tenant]}}});
    /* l1_vt is unused. */
    pipe->l1_vt[tenant] = 0;
  }
  for (uint64_t tc = 0; tc < num_tcs; tc++) {
    uint64_t tenant = (tc >> 1);
    tcArguments[tc] = {2, QueueType::PRIORITY_QUEUE, tc};
    tcLut.push_back({tc, {Operation::DEQ, {tcArguments[tc]}}});
    pipe->tc_to_pifoargs[tc] = {root_pifo_args, tenantArguments[tenant], tcArguments[tc]};
    pipe->tc_to_sattrs[tc] = {{FIELD_TENANT, tenant}, {FIELD_TC, tc}};
    pipe->l2_vt[tc] = 0;
  }
  /* Loom: TODO: memset other pifoargs. */

  /* Fair sharing between tenants. */
  PIFOPipelineStage pifo1(1,
                          FIELD_TENANT,
                          tenantLut,
                          [this] (const auto & x) {
                            static std::map<uint64_t, uint64_t> last_fin_time = {};
                            uint64_t tenant = x(FIELD_TENANT);
                            uint64_t root_vt = pifo_state_.root_vt;

                            if (last_fin_time.find(tenant) == last_fin_time.end()) {
                                last_fin_time[tenant] = root_vt;
                            }

                            uint64_t deficit = FQ_DEFICIT;
                            uint64_t fin_time = last_fin_time[tenant];
                            if (fin_time > deficit) {
                              fin_time = std::max(fin_time, root_vt - deficit);
                            } else{
                              fin_time = std::max(fin_time, root_vt);
                            }


                            /* This is bad and could lead to packet bursts from
                             * all of the dataq's in the same traffic class
                             * because we don't know how many bytes will be
                             * sent until after we check the queue. */
                            /* Another way of saying this: we're only doing
                             * per-batch fair queuing, not per-byte fair
                             * queueing */
                            //fin_time += 1; 
                            fin_time += (x(FIELD_UNACCNT_BYTES) + 1);

                            /* Workaround to save the virtual time used for
                             * this dataq despite it being constant. */ 
                            /* Loom: Note: TODO: The PIFO design seems
                             * fundamentally broken/limiting right now.  There
                             * should also be a lambda for saving shared state
                             * on dequeue operations. */
                            inc_data_qs_[x(FIELD_DATAQ_NUM)].pifo_entry(FIELD_ROOT_VT) = fin_time;
                            last_fin_time.at(tenant) = fin_time;

                            return fin_time;
                          });
  /* Strict priority between traffic classes */
  /* Note: no need for tenant virtual time in this model. */
  PIFOPipelineStage pifo2(num_tcs,
                          FIELD_TC,
                          tcLut,
                          [this] (const auto & x) {
                            return x(FIELD_TC);
                          });

  /* FIFO for each tenant. */
  PIFOPipelineStage pifo3(num_tcs,
                          FIELD_PTR,
                          {{0, {Operation::TRANSMIT, {}}}},
                          [this] (const auto & x) {
                            static std::map<uint64_t, uint64_t> last_fin_time = {};
                            uint64_t tc = x(FIELD_TC);
                            uint64_t dataq = x(FIELD_DATAQ_NUM);
                            uint64_t l2_vt = pifo_state_.l2_vt[tc];
                                
                            if ((tc % 2) == 0) {
                                return x(FIELD_XMIT_TS);
                            } else {
                                if (last_fin_time.find(dataq) == last_fin_time.end()) {
                                    last_fin_time[dataq] = l2_vt;
                                }

                                uint64_t deficit = FQ_DEFICIT;
                                uint64_t fin_time = last_fin_time[dataq];
                                if (fin_time > deficit) {
                                  fin_time = std::max(fin_time, l2_vt - deficit);
                                } else{
                                  fin_time = std::max(fin_time, l2_vt);
                                }

                                //fin_time += 1; 
                                fin_time += (x(FIELD_UNACCNT_BYTES) + 1);

                                inc_data_qs_[dataq].pifo_entry(FIELD_L2_VT) = fin_time;
                                last_fin_time.at(dataq) = fin_time;

                                return fin_time;
                            }
                          });
  pipe->mesh = new PIFOPipeline({pifo1, pifo2, pifo3,});

  return (0);
}


/* Overview of the scheduling algorithm:
 * - There are 8 total tenants
 *   - Different tenants fairly share available bandwidth
 * - Each tenant has two traffic classes (t<<1, (t<<1)+1):
 *   - E.g., Tenant 0 has tcs {0, 1}, Tenant 1 has tcs {2, 3}, ...
 *   - TC t has strict priority over traffic class t+1 
 *   - TC t uses FIFO scheduling
 *   - TC t+1 uses per-dataq fair queuing (FQ)
 * - Rate-limit class 1 is rate-limited to 2Gbps
 */
int LoomVPort::InitPifoMeshMTenantPriFairRl() {
  struct pifo_pipeline_state *pipe = &pifo_state_;
  uint64_t num_tenants = 8;
  uint64_t num_tcs = num_tenants << 1;

  assert(num_tenants <= SN_MAX_TENANT);
  assert(num_tcs <= SN_MAX_TC);

  /* Loom: DEBUG */
  LOG(INFO) << "InitPifoMeshMTenantPriFair";

  /* Init the PIFO arguments for each stage. */
  PIFOArguments root_pifo_args = {0, QueueType::PRIORITY_QUEUE, 0};
  std::map<uint64_t, PIFOArguments> tenantArguments;
  std::vector<std::pair<const int, NextHop>> tenantLut;
  std::map<uint64_t, PIFOArguments> tcArguments;
  std::vector<std::pair<const int, NextHop>> tcLut;
  pipe->root_vt = 0;
  for (uint64_t tenant = 0; tenant < num_tenants; tenant++) {
    tenantArguments[tenant] = PIFOArguments({1, QueueType::PRIORITY_QUEUE, tenant});
    tenantLut.push_back({tenant, {Operation::DEQ, {tenantArguments[tenant]}}});
    /* l1_vt is unused. */
    pipe->l1_vt[tenant] = 0;
  }
  for (uint64_t tc = 0; tc < num_tcs; tc++) {
    uint64_t tenant = (tc >> 1);
    tcArguments[tc] = {2, QueueType::PRIORITY_QUEUE, tc};
    tcLut.push_back({tc, {Operation::DEQ, {tcArguments[tc]}}});
    pipe->tc_to_pifoargs[tc] = {root_pifo_args, tenantArguments[tenant], tcArguments[tc]};
    pipe->tc_to_sattrs[tc] = {{FIELD_TENANT, tenant}, {FIELD_TC, tc}};
    pipe->l2_vt[tc] = 0;
  }
  /* Loom: TODO: memset other pifoargs. */

  /* Set the rate-limits */
  /* 2.5Gbps for the one traffic class */
  pipe->rl_class_state[1].rate_bps = (2500ull * 1000ull * 1000ull);

  /* Fair sharing between tenants. */
  PIFOPipelineStage pifo1(1,
                          FIELD_TENANT,
                          tenantLut,
                          [this] (const auto & x) {
                            static std::map<uint64_t, uint64_t> last_fin_time = {};
                            uint64_t tenant = x(FIELD_TENANT);
                            uint64_t root_vt = pifo_state_.root_vt;

                            //if (x(FIELD_RESUBMIT)) {
                            //  return x(FIELD_ROOT_VT);
                            //}

                            uint64_t deficit = FQ_DEFICIT;
                            uint64_t fin_time = last_fin_time[tenant];
                            if (fin_time > deficit) {
                              fin_time = std::max(fin_time, root_vt - deficit);
                            } else{
                              fin_time = std::max(fin_time, root_vt);
                            }

                            /* This is bad and could lead to packet bursts from
                             * all of the dataq's in the same traffic class
                             * because we don't know how many bytes will be
                             * sent until after we check the queue. */
                            /* Another way of saying this: we're only doing
                             * per-batch fair queuing, not per-byte fair
                             * queueing */
                            if (!x(FIELD_RESUBMIT)) {
                              fin_time += (x(FIELD_UNACCNT_BYTES) + 1);
                              //fin_time += 1; 
                            }

                            /* Workaround to save the virtual time used for
                             * this dataq despite it being constant. */ 
                            /* Loom: Note: TODO: The PIFO design seems
                             * fundamentally broken/limiting right now.  There
                             * should also be a lambda for saving shared state
                             * on dequeue operations. */
                            inc_data_qs_[x(FIELD_DATAQ_NUM)].pifo_entry(FIELD_ROOT_VT) = fin_time;
                            last_fin_time.at(tenant) = fin_time;

                            return fin_time;
                          });
  /* Strict priority between traffic classes */
  /* Note: no need for tenant virtual time in this model. */
  PIFOPipelineStage pifo2(num_tcs,
                          FIELD_TC,
                          tcLut,
                          [this] (const auto & x) {
                            return x(FIELD_TC);
                          });

  /* FIFO for each tenant. */
  PIFOPipelineStage pifo3(num_tcs,
                          FIELD_PTR,
                          {{0, {Operation::TRANSMIT, {}}}},
                          [this] (const auto & x) {
                            static std::map<uint64_t, uint64_t> last_fin_time = {};
                            uint64_t tc = x(FIELD_TC);
                            uint64_t dataq = x(FIELD_DATAQ_NUM);
                            uint64_t l2_vt = pifo_state_.l2_vt[tc];
                                
                            if ((tc % 2) == 0) {
                              return x(FIELD_XMIT_TS);
                            } else {
                              //if (x(FIELD_RESUBMIT)) {
                              //  return x(FIELD_L2_VT);
                              //}


                              uint64_t deficit = FQ_DEFICIT;
                              uint64_t fin_time = last_fin_time[dataq];
                              if (fin_time > deficit) {
                                fin_time = std::max(fin_time, l2_vt - deficit);
                              } else{
                                fin_time = std::max(fin_time, l2_vt);
                              }

                              if (!x(FIELD_RESUBMIT)) {
                                fin_time += (x(FIELD_UNACCNT_BYTES) + 1);
                                //fin_time += 1; 
                              }

                              inc_data_qs_[dataq].pifo_entry(FIELD_L2_VT) = fin_time;
                              last_fin_time.at(dataq) = fin_time;

                              return fin_time;
                            }
                          });
  pipe->mesh = new PIFOPipeline({pifo1, pifo2, pifo3,});

  return (0);
}

int LoomVPort::InitPifoState() {
  int ret = 0;

  struct pifo_pipeline_state *pipe = &pifo_state_;

  /* Loom: DEBUG: */
  LOG(INFO) << "InitPifoState: num_tx_ctrlqs_: " << num_tx_ctrlqs_ << 
    " num_tx_dataqs_: " << num_tx_dataqs_;

  pipe->tick = 0;

  /* One shared calendar queue. */
  {
    PIFOPipelineStage pifo1(1, FIELD_PTR, {{0, {Operation::TRANSMIT, {}}},},
      [] (const auto & x) {return x(FIELD_XMIT_TS);});
    pipe->calendar = new PIFOPipeline({pifo1,});

    /* Loom: TODO: More general rate limiting state */
    pipe->root_rl_state = {.rate_bps = 0, .unaccnt_bytes = 0, .accnt_ns = 0};

    /* Init rate-limit class state. */
    for (int rl_cls = 0; rl_cls < SN_MAX_RL_CLS; rl_cls++) {
      pipe->rl_class_state[rl_cls] = {.rate_bps = 0, .unaccnt_bytes = 0, .accnt_ns = 0};
    }
  }

  /* Init the PIFO pipeline state. */
  /* TODO: Make configurable. */
  /* TODO: Brent: I'm not sure I am not leaking memory... */
  if (sch_hier_ == SCH_FIFO) {
    InitPifoMeshFifo();
  } else if (sch_hier_ == SCH_2TEN_PRI) {
    InitPifoMesh2TenantPrio();
  } else if (sch_hier_ == SCH_2TEN_FAIR) {
    InitPifoMesh2TenantFair();
  } else if (sch_hier_ == SCH_MTEN_PRIFAIR) {
    InitPifoMeshMTenantPriFair();
  } else if (sch_hier_ == SCH_MTEN_PRIFAIR_RL) {
    InitPifoMeshMTenantPriFairRl();
  } else if (sch_hier_ == SCH_DRR) {
    LOG(ERROR) << "InitPifoState: using FIFO because DRR is requested"
      "sch_hier_: " << sch_hier_;
    InitPifoMeshFifo();
  } else {
    LOG(ERROR) << "InitPifoState: using FIFO because of an unknown "
      "sch_hier_: " << sch_hier_;
    //InitPifoMeshFifo();
    assert(0);
  }

  /* Set the per-dataq state. */
  /* Loom: TODO: refactor. */
  for (int dataq_num = 0; dataq_num < num_tx_dataqs_; dataq_num++) {
    struct tx_data_queue *dataq = &inc_data_qs_[dataq_num];
    uint64_t tc = 0;
    assert(dataq->dataq_num == dataq_num);
    dataq->active = false;
    dataq->next_packet = nullptr;
    dataq->next_xmit_ts = 0;
    dataq->next_tc = 0;
    dataq->next_rl_cls = 0;
    dataq->rl_state = {.rate_bps = 0, .unaccnt_bytes = 0, .accnt_ns = 0};
    dataq->pifo_entry(FIELD_PTR) = 0;
    dataq->pifo_entry(FIELD_DATAQ_NUM) = dataq->dataq_num;
    dataq->pifo_entry(FIELD_XMIT_TS) = rdtsc();
    dataq->pifo_entry(FIELD_TC) = tc;
    dataq->pifo_entry(FIELD_RL_NODE1) = 0;
    dataq->pifo_entry(FIELD_RESUBMIT) = 0;
    dataq->pifo_entry(FIELD_UNACCNT_BYTES) = 48000; /* Start by charging a whole batch */
    //dataq->pifo_entry(FIELD_UNACCNT_BYTES) = (bess::PacketBatch::kMaxBurst * 1500); /* Start by charging a whole batch */


    /* Init the static attributes. */
    auto sattr_pairs = pipe->tc_to_sattrs[tc];
    for (auto it = sattr_pairs.begin(); it != sattr_pairs.end(); it++) {
        dataq->pifo_entry(it->first) = it->second;
    }

    /* Init the virtual time state. */
    /* Loom: Virtual time state is a hack. See a note somewhere else. */
    //struct pifo_pipeline_state *pps = &pifo_state_;
    //for (auto it = pps->virtual_time_fields.begin();
    //     it != pps->virtual_time_fields.end(); it++) {
    //  dataq->pifo_entry(*it) = pps->virtual_times[*it];
    //}
    dataq->pifo_entry(FIELD_ROOT_VT) = 0;
    dataq->pifo_entry(FIELD_L1_VT) = 0;
    dataq->pifo_entry(FIELD_L2_VT) = 0;
  }

  return ret;
}

int LoomVPort::DeInitPifoState() {
  /* Delete objects allocated for the PIFO mesh. */
  /* Note(Brent): memory allocation doesn't seem necessary here.  However, I
   * don't completely understand the semantics of C++ constructors and
   * destructors */
  /* This should be in a destructor instead? */
  delete pifo_state_.mesh;
  pifo_state_.mesh = nullptr;
  delete pifo_state_.calendar;
  pifo_state_.calendar = nullptr;

  return (0);
}

/* Loom: TODO: remove. */
int LoomVPort::InitDrrState() {
  int ret = 0;
  uint32_t qsize;
  llring* q;

  /* Allocate the DRR ring. */
  qsize = num_tx_dataqs_ << 1;
  q = AddDrrQueue(qsize, &ret);
  LOG(INFO) << "InitDrrState: q: " << q;
  if (ret != 0) {
    return ret;
  }
  dataq_drr_.dataq_ring = q;

  /* Init metadata. */
  dataq_drr_.current_dataq = nullptr;
  dataq_drr_.quantum = DRR_QUANTUM;

  /* Set the per-dataq state. */
  for (int dataq_num = 0; dataq_num < num_tx_dataqs_; dataq_num++) {
    struct tx_data_queue *dataq = &inc_data_qs_[dataq_num];

    /* TODO: These should go somewhere else once a more general scheduling
     * algorithm is implemented. */
    dataq->active = false;
    dataq->drr_deficit = dataq_drr_.quantum;
    dataq->next_packet = nullptr;
  }

  return ret;
}

int LoomVPort::DeInitDrrState() {
  /* Loom: DEBUG: just warn for now. */
  if (llring_count(dataq_drr_.dataq_ring)) {
    LOG(WARNING) << "DeInitDrrState with items still in the dataq_drr_ queue";
  }
  
  /* Dequeue the items in the queue. */
  if (dataq_drr_.dataq_ring) {
    llring_addr_t llr_addr;
    while (llring_dequeue(dataq_drr_.dataq_ring, &llr_addr) == 0) {
      /* Loom: TODO: do something with the items still enqueued? */
    }

    /* Free memory. */
    std::free(dataq_drr_.dataq_ring);
    dataq_drr_.dataq_ring = nullptr;
  }

  return (0);
}

void LoomVPort::DeInit() {
  int ret;

  ret = DeInitSchedState();
  if (ret < 0)
    PLOG(ERROR) << "DeInitSchedState ERROR";

  ret = ioctl(fd_, SN_IOC_RELEASE_HOSTNIC);
  if (ret < 0)
    PLOG(ERROR) << "ioctl(SN_IOC_RELEASE_HOSTNIC)";

  close(fd_);
  FreeBar();
}

/* Loom: DEBUG */
static std::string get_working_path()
{
   char temp[MAXPATHLEN];
   return ( getcwd(temp, MAXPATHLEN) ? std::string( temp ) : std::string("") );
}

CommandResponse LoomVPort::Init(const bess::pb::LoomVPortArg &arg) {
  CommandResponse err;
  int ret;
  phys_addr_t phy_addr;

  struct tx_queue_opts txq_opts = tx_queue_opts();
  struct rx_queue_opts rxq_opts = rx_queue_opts();

  fd_ = -1;
  netns_fd_ = -1;
  container_pid_ = 0;
  num_tx_ctrlqs_ = 1;
  num_tx_dataqs_ = 4096; /* Loom: TODO: This should probably default to 0. */

  if (arg.ifname().length() >= IFNAMSIZ) {
    err = CommandFailure(EINVAL,
                         "Linux interface name should be "
                         "shorter than %d characters",
                         IFNAMSIZ);
    goto fail;
  }

  if (arg.ifname().length()) {
    strncpy(ifname_, arg.ifname().c_str(), IFNAMSIZ);
  } else {
    strncpy(ifname_, name().c_str(), IFNAMSIZ);
  }

  if (arg.cpid_case() == bess::pb::LoomVPortArg::kDocker) {
    err = docker_container_pid(arg.docker(), &container_pid_);
    if (err.error().code() != 0)
      goto fail;
  } else if (arg.cpid_case() == bess::pb::LoomVPortArg::kContainerPid) {
    container_pid_ = arg.container_pid();
  } else if (arg.cpid_case() == bess::pb::LoomVPortArg::kNetns) {
    netns_fd_ = open(arg.netns().c_str(), O_RDONLY);
    if (netns_fd_ < 0) {
      err = CommandFailure(EINVAL, "Invalid network namespace %s",
                           arg.netns().c_str());
      goto fail;
    }
  }

  if (arg.rxq_cpus_size() > 0 &&
      arg.rxq_cpus_size() != num_queues[PACKET_DIR_OUT]) {
    err = CommandFailure(EINVAL, "Must specify as many cores as rxqs");
    goto fail;
  }

  /* Save user configured arguments. */
  /* Loom: TODO: Make optional? Set bessctl defaults? */
  num_tx_ctrlqs_ = arg.num_tx_ctrlqs();
  num_tx_dataqs_ = arg.num_tx_dataqs();

  fd_ = open("/dev/bess", O_RDONLY);
  if (fd_ == -1) {
    err = CommandFailure(ENODEV, "the kernel module is not loaded");
    goto fail;
  }

  txq_opts.tci = arg.tx_tci();
  txq_opts.outer_tci = arg.tx_outer_tci();
  rxq_opts.loopback = arg.loopback();

  bar_ = AllocBar(&txq_opts, &rxq_opts);
  phy_addr = rte_malloc_virt2phy(bar_);

  VLOG(1) << "virt: " << bar_ << ", phys: " << phy_addr;

  ret = ioctl(fd_, SN_IOC_CREATE_HOSTNIC, &phy_addr);
  if (ret < 0) {
    err = CommandFailure(-ret, "SN_IOC_CREATE_HOSTNIC failure");
    goto fail;
  }

  if (arg.ip_addrs_size() > 0) {
    err = SetIPAddr(arg);

    if (err.error().code() != 0) {
      DeInit();
      goto fail;
    }
  }

  if (netns_fd_ >= 0) {
    close(netns_fd_);
    netns_fd_ = -1;
  }

  for (int cpu = 0; cpu < SN_MAX_CPU; cpu++) {
    map_.cpu_to_tx_ctrlq[cpu] = cpu % num_tx_ctrlqs_;
  }

  if (arg.rxq_cpus_size() > 0) {
    for (int rxq = 0; rxq < num_queues[PACKET_DIR_OUT]; rxq++) {
      map_.rxq_to_cpu[rxq] = arg.rxq_cpus(rxq);
    }
  } else {
    for (int rxq = 0; rxq < num_queues[PACKET_DIR_OUT]; rxq++) {
      next_cpu = find_next_nonworker_cpu(next_cpu);
      map_.rxq_to_cpu[rxq] = next_cpu;
    }
  }

  ret = ioctl(fd_, SN_IOC_SET_QUEUE_MAPPING, &map_);
  if (ret < 0) {
    PLOG(ERROR) << "ioctl(SN_IOC_SET_QUEUE_MAPPING)";
  }

  /* Loom: Initialize scheduling state. */
  /* Loom: TODO: enum instead of string seems better (less magic) */
  if (arg.sch_hier() == "drr") { 
    sch_hier_ = SCH_DRR;
  } else if (arg.sch_hier() == "fifo") { 
    sch_hier_ = SCH_FIFO;
  } else if (arg.sch_hier() == "2ten_pri") {
    sch_hier_ = SCH_2TEN_PRI;
  } else if (arg.sch_hier() == "2ten_fair") {
    sch_hier_ = SCH_2TEN_FAIR;
  } else if (arg.sch_hier() == "mten_prifair") {
    sch_hier_ = SCH_MTEN_PRIFAIR;
  } else if (arg.sch_hier() == "mten_prifair_rl") {
    sch_hier_ = SCH_MTEN_PRIFAIR_RL;
  } else {
    err = CommandFailure(EINVAL, "Invalid Sch Hier");
    goto fail;
  }

  ret = InitSchedState();
  if (ret < 0) {
    err = CommandFailure(-ret, "InitSchedState failure");
    goto fail;
  }

  /* Loom: DEBUG for profiling.  What directory should we expect gmon.out to be in? */
  LOG(INFO) << "cwd: " << get_working_path();

  return CommandSuccess();

fail:
  if (fd_ >= 0)
    close(fd_);

  if (netns_fd_ >= 0)
    close(netns_fd_);

  return err;
}

#if 0
static int do_ip_csum(struct bess::Packet *pkt, uint16_t csum_start,
                    uint16_t csum_dest) {
  uint16_t csum;

  /* LOOM: TODO: Better error checking. */
  assert(csum_dest != SN_TX_CSUM_DONT);

  /* LOOM:XXX: This argument should be used and rte_raw_cksum should be used
   * instead of rte_ipv4_cksum. */
  csum_dest = csum_dest;

  /* LOOM: DEBUG */
  PLOG(INFO) << " performing SN_TX_CSUM offloading!";
  PLOG(INFO) << "   csum_start:" << csum_start << ", csum_dest:" << csum_dest;

  /* LOOM: IP header approach to checksumming */
  //struct ipv4_hdr *ip;
  //ip = pkt->head_data<struct ipv4_hdr *>(csum_start);
  //csum = rte_ipv4_cksum(ip);
  /* TODO: What byte order should this be in? */
  //ip->hdr_checksum = rte_cpu_to_be_16(csum);
  //ip->hdr_checksum = csum;
  //PLOG(INFO) << "   original csum: " << ip->hdr_checksum << " (" << std::hex <<
  //  ip->hdr_checksum << ")";

  void *buf = pkt->head_data<void *>(csum_start);
  csum = rte_raw_cksum(buf, csum_dest - csum_start);
  uint16_t *pkt_csum = pkt->head_data<uint16_t *>(csum_dest);
  PLOG(INFO) << "   original csum: " << *pkt_csum << " (0x" << std::hex <<
    *pkt_csum << ")";
  //*pkt_csum = csum;


  /* LOOM: DEBUG */
  PLOG(INFO) << "   new csum: " << csum << " (0x" << std::hex << csum << ")";
  PLOG(INFO) << pkt->Dump();


  return 0;
}
#endif

/* LOOM: UGLY: */
/* The OS (sn driver) provides csum_start and csum_dest.  However, following
 * these wasn't working for me.  Rather than listening to the OS, just do what
 * we know is correct. */
static int do_ip_tcp_csum(bess::Packet *pkt) {
  struct Ethernet *eth = pkt->head_data<struct Ethernet *>();
  struct Ipv4 *ip = reinterpret_cast<struct Ipv4 *>(eth + 1);
  size_t ip_bytes = (ip->header_length) << 2;
  void *l4 = reinterpret_cast<uint8_t *>(ip) + ip_bytes;
  /* XXX: BUG: Need to check to make sure the packet is TCP! */
  struct Tcp *tcp = reinterpret_cast<struct Tcp *>(l4);

  ip->checksum = CalculateIpv4NoOptChecksum(*ip);
  tcp->checksum = CalculateIpv4TcpChecksum(*ip, *tcp);

  /* LOOM: DEBUG */
  //PLOG(INFO) << " do_ip_tcp_csum: ip csum: " << std::hex << ip->checksum << ", tcp csum: " << std::hex << tcp->checksum;

  return 0;
}

static uint16_t get_payload_offset(bess::Packet *pkt) {
  struct Ethernet *eth = pkt->head_data<struct Ethernet *>();
  struct Ipv4 *ip = reinterpret_cast<struct Ipv4 *>(eth + 1);
  size_t ip_bytes = (ip->header_length) << 2;
  void *l4 = reinterpret_cast<uint8_t *>(ip) + ip_bytes;
  /* XXX: BUG: Need to check to make sure the packet is TCP! */
  struct Tcp *tcp = reinterpret_cast<struct Tcp *>(l4);

  int org_frame_len = pkt->total_len();
  uint16_t tcp_hdrlen = (tcp->offset * 4);
  const char *datastart = ((const char *)tcp) + tcp_hdrlen;
  uint16_t payload_offset = (uint16_t)(datastart - pkt->head_data<char *>());

  if (payload_offset > org_frame_len) {
    payload_offset = org_frame_len;
  }

  return payload_offset;
}

static void do_tso(bess::Packet *pkt, uint32_t seqoffset, int first, int last) {
  /* LOOM: TODO: These could be saved instead of parsed for each packet. */
  struct Ethernet *eth = pkt->head_data<struct Ethernet *>();
  struct Ipv4 *ip = reinterpret_cast<struct Ipv4 *>(eth + 1);
  size_t ip_bytes = (ip->header_length) << 2;
  void *l4 = reinterpret_cast<uint8_t *>(ip) + ip_bytes;
  /* XXX: BUG: Need to check to make sure the packet is TCP! */
  struct Tcp *tcp = reinterpret_cast<struct Tcp *>(l4);

  uint32_t seq = tcp->seq_num.value();
  uint16_t new_ip_total_len = pkt->total_len() -
    (reinterpret_cast<uint8_t *>(ip) - pkt->head_data<uint8_t *>()); /* Check. */

  /* Update the IP header. */
  ip->length = be16_t(new_ip_total_len);
  
  /* Update the TCP Header. */
  tcp->seq_num = be32_t(seq + seqoffset);
  if (!first) /* CWR only for the first packet */
    tcp->flags &= 0x7f;	
  if (!last) /* PSH and FIN only for the last packet */
    tcp->flags &= 0xf6;

  /* LOOM: TODO: Check the packet type.  Also, VXLAN. */

  /* Just assume checksumming is needed. */
  do_ip_tcp_csum(pkt);
}


/* LOOM: TODO: at some point, I'd like to make segs from the kernel and packets
 * inside of BESS different to reduce memory pressure. */
/* Currently copy+pasta from the old RecvPackets. */
int LoomVPort::RefillSegs(queue_t qid, bess::Packet **segs, int max_cnt) {
  struct tx_data_queue *tx_queue = &inc_data_qs_[qid];
  phys_addr_t paddr[bess::PacketBatch::kMaxBurst];
  int cnt;
  int i;

  if (static_cast<size_t>(max_cnt) > bess::PacketBatch::kMaxBurst) {
    max_cnt = bess::PacketBatch::kMaxBurst;
  }
  cnt = llring_sc_dequeue_burst(tx_queue->drv_to_sn, paddr, max_cnt);

  for (i = 0; i < cnt; i++) {
    bess::Packet *seg;

    struct sn_tx_data_desc *tx_desc;
    //struct sn_tx_metadata *tx_meta;
    uint16_t len;

    seg = segs[i] = bess::Packet::from_paddr(paddr[i]);

    /* This extra work is likely unnecessary */
    tx_desc = seg->scratchpad<struct sn_tx_data_desc *>();
    len = tx_desc->total_len;
    //tx_meta = &tx_desc->meta;

    seg->set_data_off(SNBUF_HEADROOM);
    seg->set_total_len(len);
    seg->set_data_len(len);

    /* LOOM: DEBUG */
    //PLOG(INFO) << "LoomVPort::RefillSegs: received a segment of size: " << len;
  }

  return cnt;
}

/* LOOM. */
bess::Packet *LoomVPort::SegPkt(queue_t qid) {
  bess::Packet *pkt, *seg;
  //struct queue *tx_queue = &inc_qs_[qid];
  struct txq_private *txq_priv = &inc_data_qs_[qid].txq_priv;
  struct sn_tx_data_desc *tx_desc;
  struct sn_tx_data_metadata *tx_meta;

  assert((txq_priv->cur_seg < txq_priv->seg_cnt) || (txq_priv->seg_cnt == 0));

  if (txq_priv->seg_cnt == 0) {
    return nullptr;
  }

  seg = txq_priv->segs[txq_priv->cur_seg];
  if (txq_priv->payload_offset == 0) {
    txq_priv->payload_offset = get_payload_offset(seg);
    assert(txq_priv->seqoffset == 0);
  }
  assert(txq_priv->payload_offset > 0);
  assert(txq_priv->payload_offset <= seg->total_len());

  /* LOOM: TODO: I'd like to get rid of the need for this assert at some point. */
  assert(seg->is_linear());

  /* Just do passthrough if the segment is small enough. */
  /* LOOM: TODO: Check an mtu? */
  if (seg->total_len() <= FRAME_SIZE) {
    pkt = seg;

    /* Do checksumming if needed. */
    tx_desc = pkt->scratchpad<struct sn_tx_data_desc *>();
    tx_meta = &tx_desc->meta;
    if (tx_meta->csum_start != SN_TX_CSUM_DONT) {
      do_ip_tcp_csum(pkt);
    }

    /* Move on to the next segment. */
    /* TODO: Unify this with the code in the else statement below? */
    txq_priv->cur_seg++;

  /* Slice of a packet from the current segment. */
  } else {
    int org_frame_len = seg->total_len();
    int max_seg_size = FRAME_SIZE - txq_priv->payload_offset;
    int seg_size = std::min(max_seg_size,
      org_frame_len - (int) txq_priv->payload_offset - (int) txq_priv->seqoffset);

    int ret;
    int first, last;

    /* LOOM: DEBUG */
    //PLOG(INFO) << "LoomVPort::SegPkt: segmenting a large packet of size: " << org_frame_len;

    /* Get the new packet. */
    ret = llring_mc_dequeue(txq_priv->segpktpool, reinterpret_cast<llring_addr_t*>(&pkt));
    if (ret) {
      return nullptr;
    }
    /* LOOM: TODO: It seems better to use a pool to avoid allocating for every
     * packet in a segment in the future. */
    //if (!(pkt = bess::Packet::Alloc())) {
    //  return nullptr;
    //}

    /* Initialize the new packet and copy both the headers and payload. */
    pkt->set_data_off(SNBUF_HEADROOM);
    bess::utils::CopyInlined(pkt->append(txq_priv->payload_offset), seg->head_data(),
                             txq_priv->payload_offset, true);
    bess::utils::CopyInlined(pkt->append(seg_size), seg->head_data<char*>() +
                             txq_priv->payload_offset + txq_priv->seqoffset,
                             seg_size, true);

    /* Do the TSO IP/TCP header updates. */
    first = (txq_priv->seqoffset == 0);
    last = ((int) txq_priv->payload_offset + (int) txq_priv->seqoffset + 
      max_seg_size >= org_frame_len);
    do_tso(pkt, txq_priv->seqoffset, first, last);

    /* Update the seqoffset. */
    txq_priv->seqoffset += seg_size; //or max_seg_size?

    /* Move on to the next segment. */
    if ((int) txq_priv->payload_offset + (int) txq_priv->seqoffset >=
        org_frame_len) {
      bess::Packet::Free(seg);
      txq_priv->segs[txq_priv->cur_seg] = nullptr;
      txq_priv->cur_seg++;
      txq_priv->payload_offset = 0;
      txq_priv->seqoffset = 0;
    }
  }

  /* Cleanup.  Should this be elsewhere? */
  if (txq_priv->cur_seg >= txq_priv->seg_cnt) {
    txq_priv->seg_cnt = 0;
    txq_priv->cur_seg = 0;
    txq_priv->payload_offset = 0;
    txq_priv->seqoffset = 0;
  }

  return pkt;
}


/* LOOM: This code is left over from a *partially* working implementation of
 * TSO.  For now, the TSO implementation is being abandoned because GSO is good
 * enough.  This is just here to be a reference or fallback for now.  It may be
 * useful in getting BQL and TCP Small Queues. */
#if 0
int LoomVPort::RecvPackets(queue_t qid, bess::Packet **pkts, int max_cnt) {
  //struct queue *tx_queue = &inc_qs_[qid];
  struct txq_private *txq_priv = &inc_qs_[qid].txq_priv;
  bess::Packet *pkt;
  int cnt = 0;

  if (txq_priv->seg_cnt == 0) {
    txq_priv->seg_cnt = this->RefillSegs(qid, txq_priv->segs, bess::PacketBatch::kMaxBurst);
    txq_priv->cur_seg = 0;
  }

  /* Is this slow? Should this be done somewhere else? */
  /* LOOM: Note: internally, refill_segpktpool does nothing unless the number of
   * free packets are below a low water mark. */
  refill_segpktpool(txq_priv->segpktpool);

  refill_tx_bufs(tx_queue->sn_to_drv);

  pkt = this->SegPkt(qid);
  while (cnt < max_cnt && pkt != nullptr) {
    pkts[cnt] = pkt; 
    cnt++;

    pkt = this->SegPkt(qid);
  }

  /* LOOM: DEBUG */
  //if (cnt > 0) {
  //  PLOG(INFO) << "LoomVPort::RecvPackets: returning a batch of size: " << cnt;
  //}

  return cnt;
}
#endif

int LoomVPort::RecvPackets(queue_t qid, bess::Packet **pkts, int max_cnt) {
  return RecvPacketsDataQ(qid, pkts, max_cnt);
}

int LoomVPort::DequeueCtrlDescs(struct queue *tx_ctrl_queue,
                             struct sn_tx_ctrl_desc *ctrl_desc_arr,
                             int max_cnt)
{
  int dequeue_obj_cnt;
  int ctrl_desc_cnt = 0;
  int ret;

  /* Loom: DEBUG */
  //if (llring_count(tx_ctrl_queue->drv_to_sn) > 0) {
  //  PLOG(INFO) << bess::utils::Format("DequeueCtrlDesc: ctrl_queue ring "
  //    "count: %d", llring_count(tx_ctrl_queue->drv_to_sn));
  //}

  ctrl_desc_cnt = std::min(max_cnt, (int)(llring_count(tx_ctrl_queue->drv_to_sn) / SN_OBJ_PER_TX_CTRL_DESC));
  dequeue_obj_cnt = ctrl_desc_cnt * SN_OBJ_PER_TX_CTRL_DESC;

  if (dequeue_obj_cnt == 0) {
    return 0;
  }

  if (llring_count(tx_ctrl_queue->drv_to_sn) % SN_OBJ_PER_TX_CTRL_DESC != 0) {
    PLOG(ERROR) << bess::utils::Format("Unexpected number of objs: %d", 
      llring_count(tx_ctrl_queue->drv_to_sn));
  }

  ret = llring_mc_dequeue_bulk(tx_ctrl_queue->drv_to_sn,
    (llring_addr_t *)ctrl_desc_arr, dequeue_obj_cnt);
  if (ret != 0) {
    ctrl_desc_cnt = 0;
    PLOG(ERROR) << bess::utils::Format("Unable to dequeue %d objs from "
      "tx_ctrl_queue TODO", dequeue_obj_cnt);
  }

  /* Loom: DEBUG: Just print the ctrl descriptors for now. */
#if 0
  int i;
  for (i = 0; i < ctrl_desc_cnt; i++) {
    struct sn_tx_ctrl_desc *ctrl_desc = &ctrl_desc_arr[i];

    PLOG(INFO) << bess::utils::Format("tx_ctrl_desc: cookie: %x, dataq: %d",
      ctrl_desc->cookie, ctrl_desc->dataq_num);
  }
#endif

  return ctrl_desc_cnt;
}

int LoomVPort::ProcessCtrlDescs(struct sn_tx_ctrl_desc *ctrl_desc_arr,
                            int cnt)
{
  int i;
  for (i = 0; i < cnt; i++) {
    struct sn_tx_ctrl_desc *ctrl_desc = &ctrl_desc_arr[i];

    /* Sanity check */
    if (ctrl_desc->cookie != SN_CTRL_DESC_COOKIE) {
      PLOG(ERROR) << bess::utils::Format("Bad Ctrl Desc Cookie: %x",
        ctrl_desc->cookie);
      continue;
    }
    if ((int)ctrl_desc->dataq_num >= num_tx_dataqs_) {
      PLOG(ERROR) << bess::utils::Format("Bad TX Dataq num: %d",
        ctrl_desc->dataq_num);
      continue;
    }

    /* Loom: Read scheduling metadata from ctrl descriptors. */
    if (sch_hier_ == SCH_DRR) {
      AddNewDrrDataq(ctrl_desc);
    } else {
      AddNewPifoDataq(ctrl_desc);
    }
  }

  /* No error. */
  return (0);
}

int LoomVPort::AddNewPifoDataq(struct sn_tx_ctrl_desc *ctrl_desc) {
  struct tx_data_queue *dataq;
  uint64_t tc = ctrl_desc->meta.sch_meta.tc;

  /* Loom: TODO: Better error checking (and security?) on the incoming traffic
   * class. */
  assert(tc < SN_MAX_TC); /* TC is used as an index! It should not allow for overflow. */

  /* Loom: Read scheduling metadata from ctrl descriptors and save to the
   * dataq state. */
  dataq = &inc_data_qs_[ctrl_desc->dataq_num];
  if (!dataq->active) {
    /* Loom: TODO: different data structure. */
    assert(dataq->active == false);
    dataq->active = true;
    assert(dataq->next_packet == nullptr);
    dataq->next_packet = nullptr;

    /* Loom: TODO: Read the first packet into next_packet to get its
     * scheduling metadata (if needed). */
    /* Loom: TODO: Read from the control descriptor instead? */
    dataq->next_xmit_ts = rdtsc();

    /* Loom: Set PIFO scheduling metadata. */
    dataq->pifo_entry(FIELD_XMIT_TS) = dataq->next_xmit_ts;
    dataq->pifo_entry(FIELD_TC) = tc;

    /* Loom: DEBUG. */
    //LOG(INFO) << bess::utils::Format("ProcessCtrlDescs: Adding dataq_num %lu "
    //  "to PIFO mesh with xmit_ts %lu and tc %lu", dataq->pifo_entry(FIELD_DATAQ_NUM),
    //  dataq->pifo_entry(FIELD_XMIT_TS), dataq->pifo_entry(FIELD_TC));

    /* Loom: Enqueue the dataq in the PIFO mesh. */
    AddDataqToPifo(dataq);
  }

  /* No error. */
  return (0);
}

int LoomVPort::AddDataqToPifo(struct tx_data_queue *dataq) {
  uint64_t tc = dataq->pifo_entry(FIELD_TC);
  uint64_t tenant = dataq->pifo_entry(FIELD_TENANT);

  /* Loom: TODO: Better error checking. */
  assert(dataq->active);

  /* Loom: TODO: Better error checking. */
  assert(tc < SN_MAX_TC);
  assert(tenant < SN_MAX_TENANT);

  /* Loom: DEBUG */
  //LOG(INFO) << bess::utils::Format("AddDataqToPifo: Adding dataq_num %lu "
  //  "to PIFO mesh with xmit_ts %lu and tc %lu", dataq->pifo_entry(FIELD_DATAQ_NUM),
  //  dataq->pifo_entry(FIELD_XMIT_TS), dataq->pifo_entry(FIELD_TC));

  /* Reset all static attributes for the traffic class in case it has changed
   * and they need to be reset. */
  /* Loom: TODO: This could be more efficient. */
  auto sattr_pairs = pifo_state_.tc_to_sattrs[tc];
  for (auto it = sattr_pairs.begin(); it != sattr_pairs.end(); it++) {
      dataq->pifo_entry(it->first) = it->second;
  }

  /* Save the the virtual time state. */
  /* Loom: Virtual time state is a hack. See a note somewhere else. */
  struct pifo_pipeline_state *ps = &pifo_state_;
  //for (auto it = ps->virtual_time_fields.begin();
  //     it != ps->virtual_time_fields.end(); it++) {
  //  dataq->pifo_entry(*it) = ps->virtual_times[*it];
  //
  //  /* Loom: DEBUG */
  //  //LOG(INFO) << bess::utils::Format("  AddDataqToPifo: For dataq_num %lu "
  //  //  "setting field %s -> %lu", dataq->pifo_entry(FIELD_DATAQ_NUM),
  //  //  it->c_str(), dataq->pifo_entry(*it));
  //}
  /* Loom: HACK: Requires that layer-1 nodes in the tree are identified by
   * unique "tenant" ids and that layer-2 nodes in the tree are identified by
   * unique "tc" ids. */
  /* Loom: NOTE: A better approach would be to keep generic state and to
   * identify this state from node ids in the scheduling tree. Also would
   * require a mapping from dataq -> list of node ids. */
  if (!dataq->pifo_entry(FIELD_RESUBMIT)) {
    dataq->pifo_entry(FIELD_ROOT_VT) = ps->root_vt;
    dataq->pifo_entry(FIELD_L1_VT) = ps->l1_vt[dataq->pifo_entry(FIELD_TENANT)];
    dataq->pifo_entry(FIELD_L2_VT) = ps->l2_vt[dataq->pifo_entry(FIELD_TC)];
  }


  /* Loom: TODO: More DEBUG. Print new attrs */
  //LOG(INFO) << "  PIFO entry: " << dataq->pifo_entry;

  assert(tc < SN_MAX_TC); /* Loom: TODO: Error checking */
  auto pifo_arg_vec = &pifo_state_.tc_to_pifoargs[tc];
  for (size_t i = 0; i < pifo_arg_vec->size(); i++) {
    const auto pifo_args = pifo_arg_vec->at(i);
    pifo_state_.mesh->enq(pifo_args.stage_id, pifo_args.q_type,
      pifo_args.queue_id, dataq->pifo_entry, pifo_state_.tick);
  }
  pifo_state_.tick++;


  /* Loom: TODO: More DEBUG. Print the entire PIFO */
  //LOG(INFO) << "  PIFO: " << pifo_state_.mesh;

  return (0);
}

/* Loom: Note: I would expect this to break in bad ways if different workers
 * are polling different control queues at the same time because they will try
 * to access the same dataq_ring. */
int LoomVPort::GetNextPifoBatch(bess::Packet **pkts, int max_cnt) {
  struct tx_data_queue *dataq;
  uint64_t total_bytes = 0;
  uint64_t now_ns = tsc_to_ns(rdtsc());
  int cnt = 0;
  int ret;

  // iterate through flows in round robin fashion until batch is full
  while (cnt < max_cnt) {
    dataq = GetNextPifoDataq();
    if (dataq == nullptr) {
      /* TODO: revisit */
      break;
    }

    assert(dataq->pifo_entry(FIELD_TENANT) < SN_MAX_TENANT);
    assert(dataq->pifo_entry(FIELD_TC) < SN_MAX_TC);

    /* Loom: XXX: HACK: Save virtual time state needed for FQ computation */
    /* Loom: Note: this would be better if the lambda given to the
     * PIFOPipelineState was called on enq and deq so that state could be saved
     * on deq (locally) in the lambda. */
    struct pifo_pipeline_state *ps = &pifo_state_;
    //for (auto it = ps->virtual_time_fields.begin();
    //     it != ps->virtual_time_fields.end(); it++) {
    //  ps->virtual_times[*it] = dataq->pifo_entry(*it);
    //  //LOG(INFO) << bess::utils::Format("  GetNextPifoBatch: For dataq_num %lu "
    //  //  "setting virtual time field %s -> %lu", dataq->pifo_entry(FIELD_DATAQ_NUM),
    //  //  it->c_str(), ps->virtual_times[*it]);
    //}

    /* Loom: XXX: This should be more general. See other note. */
    ps->root_vt = dataq->pifo_entry(FIELD_ROOT_VT);
    ps->l1_vt[dataq->pifo_entry(FIELD_TENANT)] = dataq->pifo_entry(FIELD_L1_VT);
    ps->l2_vt[dataq->pifo_entry(FIELD_TC)] = dataq->pifo_entry(FIELD_L2_VT);

    /* Get an entire batch of packets from a single DataQ if possible.  This is
     * a performance vs. precision/granularity of network scheduling trade-off.
     * */
    ret = GetNextPifoPackets(&pkts[cnt], max_cnt - cnt, dataq, &total_bytes);
    cnt += ret;

    /* Loom: DEBUG */
    //LOG(INFO) << bess::utils::Format("Dataq %d sent %lu bytes",
    //  dataq->dataq_num, total_bytes);

    /* Accounting for rate-limiting. */
    //assert(dataq->rl_state.unaccnt_bytes == 0);
    //dataq->rl_state.unaccnt_bytes += total_bytes;
    /* Reset. Assume they have been accounted for. */
    dataq->rl_state.unaccnt_bytes = total_bytes;
    dataq->rl_state.accnt_ns = now_ns;

    /* Set scheduilng metadata. */
    /* Loom: TODO: Modularize and make generic. */
    /* Loom: TODO: start here: updates total_bytes for all of the static
     * attributes for this traffic class */
    /* Loom: TODO: start here: save total_bytes to the virtual time for all of
     * the static attributes for this traffic class */

    /* Loom: Note: GetNextPifoPackets can set dataq->next_tc. */
    /* Loom: TODO: Better error checking. */
    assert(dataq->next_tc < SN_MAX_TC);

    /* Loom: TODO: Ordering? */
    dataq->pifo_entry(FIELD_XMIT_TS) = dataq->next_xmit_ts;
    dataq->pifo_entry(FIELD_TC) = dataq->next_tc;
    dataq->pifo_entry(FIELD_RL_NODE1) = dataq->next_rl_cls;
    dataq->pifo_entry(FIELD_RESUBMIT) = 0;
    /* Does not account correctly when traffic classes are changed.*/
    /* Loom: Note: Much of this implementation has problems when a queue changes traffic classes. */
    dataq->pifo_entry(FIELD_UNACCNT_BYTES) = total_bytes; 

    /* TODO: What to do if the dataq does not have any packets? */
    /* TODO: This feels redundant wiht GetNextPifoDataq */
    if (llring_empty(dataq->drv_to_sn) && !dataq->next_packet) {
      dataq->active = false;
      assert(dataq->next_packet == nullptr);

    } else {
      /* Re-enqueue the flow. */
      assert(dataq->active);

      /* Loom: DEBUG */
      // TODO: Uncommenting this is useful for debugging if traffic classes are
      // assigned correctly.
      //LOG(INFO) << bess::utils::Format("GetNextPifoBatch: Adding dataq_num %lu "
      //  "to PIFO mesh with xmit_ts %lu and tc %lu", dataq->pifo_entry(FIELD_DATAQ_NUM),
      //  dataq->pifo_entry(FIELD_XMIT_TS), dataq->pifo_entry(FIELD_TC));

      /* Loom: Enqueue the dataq in the PIFO mesh. */
      AddDataqToPifo(dataq);
    }

  }

  return cnt;
}

int LoomVPort::ShouldRlPifoDataq(struct tx_data_queue *dataq) {
  uint64_t rl_cls = dataq->pifo_entry(FIELD_RL_NODE1);
  struct rate_limit_state *rl_state = nullptr;
  uint64_t rl_bps = 0;
  uint64_t now_ns = tsc_to_ns(rdtsc());
  uint64_t next_ns, start_ns, rl_ns;
  int ret = 0;

  assert(rl_cls < SN_MAX_RL_CLS);
  rl_state = &pifo_state_.rl_class_state[rl_cls];
  rl_bps = rl_state->rate_bps;
  if (rl_bps == 0) {
    /* Loom: TODO: This code is broken if we want to apply multiple different
     * rate-limiter classes in a hierarchy.  Unaccnt_bytes should only be reset
     * after *all* rate limit computation has been performed. */
    dataq->rl_state.unaccnt_bytes = 0;
    return 0;
  }

  /* Loom: DEBUG */
  //LOG(INFO) << bess::utils::Format("dataq %d rl state: unaccnt_bytes: %lu, "
  //  "accnt_ns: %lu", dataq->dataq_num, dataq->rl_state.unaccnt_bytes,
  //  dataq->rl_state.accnt_ns);

  /* Loom: TODO: Only do global rate-limiting for now. */

  start_ns = rl_state->accnt_ns;
  rl_ns = ((dataq->rl_state.unaccnt_bytes * 8) * (1000 * 1000 * 1000)) / rl_bps;
  next_ns = start_ns + rl_ns;

  /* Loom: DEBUG */
  //LOG(INFO) << bess::utils::Format("total_bytes: %lu, rl_ns: %lu",
  //  dataq->rl_state.unaccnt_bytes, rl_ns);

  /* Check if we should be rate-limited */
  if (next_ns < now_ns) {
    /* Loom(Brent): I'm not convinced that this line is correct. */
    //next_ns = now_ns;
    /* Loom(Brent): Setting next_ns to the time the unaccounted bytes were sent
     * should ensure that the rate is respected for each batch. */
    //next_ns = dataq->rl_state.accnt_ns;
    next_ns = dataq->rl_state.accnt_ns + rl_ns;
    /* Loom(Brent): I really should think about this some more. */

    assert(dataq->rl_state.accnt_ns < now_ns);

    /* This line is (likely) unnecessary. */
    dataq->pifo_entry(FIELD_XMIT_TS) = next_ns;
    ret = 0;
  } else {
    //LOG(INFO) << bess::utils::Format("dataq %d would be rate-limited", dataq->dataq_num);
    dataq->pifo_entry(FIELD_XMIT_TS) = next_ns;
    ret = 1;
  }

  /* Save the next allowed transmit time. */
  rl_state->accnt_ns = next_ns;

  /* The previously sent bytes have now been accounted for. */
  /* Loom: TODO: This code is broken if we want to apply multiple different
   * rate-limiter classes in a hierarchy.  Unaccnt_bytes should only be reset
   * after *all* rate limit computation has been performed. */
  dataq->rl_state.unaccnt_bytes = 0;
  /* Let the timestamp be? */

  return ret;
}

LoomVPort::tx_data_queue* LoomVPort::GetNextPifoDataq() {
  Optional<PIFOPacket> ret;
  PIFOPacket pifo_entry;
  struct tx_data_queue *dataq;

  /* Try to get something from the calendar queue first. */
  /* Loom: Stolen from modules/timestamp.cc: We don't use ctx->current_ns here
   * for better accuracy */
  uint64_t now_ns = tsc_to_ns(rdtsc());
  ret = pifo_state_.calendar->deq(0, QueueType::CALENDAR_QUEUE, 0, now_ns);
  while (ret.initialized()) {
    pifo_entry = ret.get();
    dataq = &inc_data_qs_[pifo_entry(FIELD_DATAQ_NUM)];
    assert(dataq->active);

    dataq->pifo_entry(FIELD_RESUBMIT) = 1;
    AddDataqToPifo(dataq);

    ret = pifo_state_.calendar->deq(0, QueueType::CALENDAR_QUEUE, 0, now_ns);
    //return dataq;
  }
  /* Loom: To be general, it should be allowed for the rate-limited queue to be
   * re-inserted into (some of the) PIFO mesh.  This implementation is not
   * general yet. */

  /* Try to dequeue from the mesh if there is nothing in the calendar queue. */
  while (true) {
    ret = pifo_state_.mesh->deq(0, QueueType::PRIORITY_QUEUE, 0,
      pifo_state_.tick);
    pifo_state_.tick++;

    if (!ret.initialized()) {
      return nullptr;
    } else {
      pifo_entry = ret.get();
    }

    dataq = &inc_data_qs_[pifo_entry(FIELD_DATAQ_NUM)];
    assert(dataq->active);

    /* Rate-limit the dataq as needed. */
    /* Loom: TODO */
    if (ShouldRlPifoDataq(dataq)) {
      /* Loom: DEBUG. */
      //LOG(INFO) << bess::utils::Format("Rate-limiting dataq_num %d "
      //  "with xmit_ts %lu", dataq->dataq_num, dataq->pifo_entry(FIELD_XMIT_TS));

      /* This current implementation is broken in a fundamental way.  Imagine a
       * high priority and full tput flow share the same rate-limit class.  In
       * this current implementation, when the tput flow is rate-limited and
       * then a high priority flow shows up, the tput flow still gets to go
       * first.
       * To fix this, only one request per child-tree should be enqueued in the
       * calendar queue.  The rate-limited queues should then be enqueued in
       * their own separate copy of the same hierarchy.
       * (Performance/implementation? Could be too wasteful unless logical
       * queues can share the same total queuing capacity. I think it should be
       * doable though.)
       */
      /* Loom: TODO: only enqueue a single rate-limit reference per rate-limit
       * class */
      pifo_state_.calendar->enq(0, QueueType::CALENDAR_QUEUE, 0,
        dataq->pifo_entry, now_ns);
    } else {
      //break;
      return dataq;
    }
  }

  /* Loom: DEBUG. */
  //LOG(INFO) << bess::utils::Format("Dequeued dataq_num %lu from PIFO mesh "
  //  "with xmit_ts %lu", pifo_entry(FIELD_DATAQ_NUM), pifo_entry(FIELD_XMIT_TS));

  /* Loom: TODO: More DEBUG. Print new attrs */
  //LOG(INFO) << "  PIFO entry: " << dataq->pifo_entry;

  /* Loom: TODO: More DEBUG. Print the entire PIFO */
  //LOG(INFO) << "  PIFO: " << pifo_state_.mesh;

  return dataq;
}

/* Loom: TODO: Performance: Read a batch of packets */
int LoomVPort::GetNextPifoPackets(bess::Packet **pkts, int max_cnt,
                               struct tx_data_queue *dataq, uint64_t *total_bytes) {
  int cnt;
  bess::Packet* pkt;
  uint64_t total_bytes_ret = 0;

  cnt = 0;
  while (cnt < max_cnt && (!llring_empty(dataq->drv_to_sn) || dataq->next_packet)) {
    /* Loom: Note: next_packet was used by the modules/drr.c implementation I initially used */
    assert(dataq->next_packet == nullptr);

    // makes sure there isn't already a packet at the front
    if (!dataq->next_packet) {
      pkt = DataqReadPacket(dataq);
      if (pkt == nullptr) {
        PLOG(ERROR) << "Unable to dequeue packet from dataq!";
        *total_bytes = total_bytes_ret;
        return cnt;
      }
    } else {
      pkt = dataq->next_packet;
      dataq->next_packet = nullptr;
    }

    /* Loom: for now, always try to read a full back from a dataq. */
    /* Loom: TODO: break conditions for different scheduling algorithms? */

    pkts[cnt] = pkt;
    cnt++;
    total_bytes_ret += pkt->total_len();
  }

  /* Loom: DEBUG */
  //LOG(INFO) << "Read " << cnt << " packets from dataq: " << 
  //  dataq->dataq_num;

  *total_bytes = total_bytes_ret;
  return cnt;
}

int LoomVPort::AddNewDrrDataq(struct sn_tx_ctrl_desc *ctrl_desc) {
  struct tx_data_queue *dataq;

  dataq = &inc_data_qs_[ctrl_desc->dataq_num];
  if (!dataq->active) {
    /* Loom: TODO: different data structure. */
    assert(dataq->active == false);
    dataq->active = true;
    /* Loom: TODO: For some reason this assertion fails... */
    //assert(dataq->next_packet == nullptr);
    //dataq->next_packet = nullptr;

    /* Loom: TODO: remove naive DRR dataq scheduling. */
    /* Add the dataq to the DRR queue (at the back. ouch.) */
    dataq->drr_deficit = 0;
    int err = llring_enqueue(dataq_drr_.dataq_ring, reinterpret_cast<llring_addr_t>(dataq));
    if (err) {
      PLOG(ERROR) << bess::utils::Format("Unable to add dataq %d to DRR ring",
        ctrl_desc->dataq_num);
      return -EINVAL;
    }
  }

  return (0);
}

/* Loom: Note: I would expect this to break in bad ways if different workers
 * are polling different control queues at the same time because they will try
 * to access the same dataq_ring. */
int LoomVPort::GetNextDrrBatch(bess::Packet **pkts, int max_cnt) {
  struct tx_data_queue *dataq;
  uint32_t dataq_count = llring_count(dataq_drr_.dataq_ring);
  if (dataq_drr_.current_dataq) {
    dataq_count++;
  }
  int last_round_cnt = 0;
  int cnt = 0;
  int ret;

  // iterate through flows in round robin fashion until batch is full
  while (cnt < max_cnt) {
    // checks to see if there has been no update after a full round
    // ensures that if every flow is empty or if there are no flows
    // that will terminate with a non-full batch.
    /* Loom: TODO: Since empty dataq's are removed this is probably overly complex. */
    if (dataq_count == 0) {
      if (last_round_cnt == cnt) {
        break;
      } else {
        dataq_count = llring_count(dataq_drr_.dataq_ring);
        last_round_cnt = cnt;
      }
    }
    dataq_count--;

    dataq = GetNextDrrDataq();
    if (dataq == nullptr) {
      /* TODO: revisit */
      continue;
    }

    ret = GetNextDrrPackets(&pkts[cnt], max_cnt - cnt, dataq);
    cnt += ret;

    /* TODO: What to do if the dataq does not have any packets? */
    /* TODO: This feels redundant wiht GetNextDrrDataq */
    if (llring_empty(dataq->drv_to_sn) && !dataq->next_packet) {
      dataq->drr_deficit = 0;
      dataq->active = false;
      assert(dataq->next_packet == nullptr);
    }

    // if the flow doesn't have any more packets to give, reenqueue it
    if (!dataq->next_packet || (uint32_t)dataq->next_packet->total_len() > dataq->drr_deficit) {
      ret = llring_enqueue(dataq_drr_.dataq_ring, reinterpret_cast<llring_addr_t>(dataq));
      if (ret != 0) {
        PLOG(ERROR) << "Unable to enqueue a dataq back into the DRR queue!";
        dataq->active = false;
        dataq->drr_deficit = 0;
        if (dataq->next_packet) {
          bess::Packet::Free(dataq->next_packet);
          dataq->next_packet = nullptr;
        }
        break;
      }
    } else {
      // knowing that the while statement will exit, keep the flow that still
      // has packets at the front
      assert(cnt >= max_cnt);
      dataq_drr_.current_dataq = dataq;
    }
  }

  return cnt;
}

LoomVPort::tx_data_queue* LoomVPort::GetNextDrrDataq() {
  struct tx_data_queue *dataq;
  int err;

  if (!dataq_drr_.current_dataq) {
    err = llring_dequeue(dataq_drr_.dataq_ring, reinterpret_cast<llring_addr_t*>(&dataq));
    if (err < 0) {
      PLOG(ERROR) << "Unable to get a next dataq!";
      return nullptr;
    }

    if (llring_empty(dataq->drv_to_sn) && !dataq->next_packet) {
      // if the flow expired, remove it and update it
      /* Loom: TODO: Move to its own function. */
      dataq->active = false;
      dataq->drr_deficit = 0;
      assert(dataq->next_packet == nullptr);

      return nullptr;
    }

    dataq->drr_deficit += dataq_drr_.quantum;
  } else {
    dataq = dataq_drr_.current_dataq;
    dataq_drr_.current_dataq = nullptr;
  }

  return dataq;
}

int LoomVPort::GetNextDrrPackets(bess::Packet **pkts, int max_cnt,
                               struct tx_data_queue *dataq) {
  int cnt;
  bess::Packet* pkt;

  cnt = 0;
  while (cnt < max_cnt && (!llring_empty(dataq->drv_to_sn) || dataq->next_packet)) {
    // makes sure there isn't already a packet at the front
    if (!dataq->next_packet) {
      pkt = DataqReadPacket(dataq);
      if (pkt == nullptr) {
        PLOG(ERROR) << "Unable to dequeue packet from dataq!";
        return cnt;
      }
    } else {
      pkt = dataq->next_packet;
      dataq->next_packet = nullptr;
    }

    if ((uint32_t)pkt->total_len() > dataq->drr_deficit) {
      dataq->next_packet = pkt;
      break;
    }

    dataq->drr_deficit -= pkt->total_len();

    pkts[cnt] = pkt;
    cnt++;
  }

  /* Loom: DEBUG */
  //LOG(INFO) << "Read " << cnt << " packets from dataq: " << 
  //  dataq->dataq_num;

  return cnt;
}

//Likely borked on BE machines
#define IP_10_10_102_1 (0x01660a0a)
#define IP_10_10_102_2 (0x02660a0a)

/* Loom: TODO: This should be a general lambda called whenever an element is
 * dequeued from a PIFO stage. */
static uint64_t get_next_rl_cls(struct sn_tx_data_metadata *tx_meta) {
  uint64_t rl_cls = 0; /* 0 is a null class */

  /* LOOM: DEBUG */
  //LOG(INFO) << bess::utils::Format("tx_meta: daddr: %x", tx_meta->skb_daddr);

  /* Hardcode classes for now. */
  if (tx_meta->skb_daddr == IP_10_10_102_1 ||
      tx_meta->skb_daddr == IP_10_10_102_2) {
    rl_cls = 1;
  }

  return rl_cls;
}

/* Loom: TODO: Read a batch? Do TSO? */
bess::Packet* LoomVPort::DataqReadPacket(struct tx_data_queue *dataq) {
  phys_addr_t paddr;
  struct sn_tx_data_desc *tx_desc;
  struct sn_tx_data_metadata *tx_meta;
  bess::Packet *pkt;
  uint16_t len;
  int err;

  err = llring_dequeue(dataq->drv_to_sn, reinterpret_cast<llring_addr_t*>(&paddr));
  if (err != 0) {
    PLOG(ERROR) << "Unable to dequeue packet from dataq that should should have data!";
    return nullptr;
  }

  pkt = bess::Packet::from_paddr(paddr);

  tx_desc = pkt->scratchpad<struct sn_tx_data_desc *>();
  len = tx_desc->total_len;
  tx_meta = &tx_desc->meta;

  pkt->set_data_off(SNBUF_HEADROOM);
  pkt->set_total_len(len);
  pkt->set_data_len(len);

  /* TODO: process sn_tx_metadata */
  /* Loom: TODO: make more general for updating dataq scheduling config. */
  /* Loom: TODO: Why not just directly update the pifo_entry? */
  dataq->next_xmit_ts = tx_meta->drv_xmit_ts;
  dataq->next_tc = tx_meta->skb_priority; 
  /* Loom: TODO: Performance: it would probably be best to not compute the next
   * rate-limit class for every packet but instead just compute it once
   * per-batch. */
  /* Loom: TODO: It should be possible to assign a dataq to multiple rate limit classes. */
  dataq->next_rl_cls = get_next_rl_cls(tx_meta);

  /* Loom: DEBUG */
  //LOG(INFO) << bess::utils::Format("dataq %d has rate-limit class: %lu",
  //  dataq->dataq_num, dataq->next_rl_cls);

  /* Loom: DEBUG. */
  if (tx_meta->skb_priority != tx_meta->sch_meta.tc) {
    LOG(WARNING) << bess::utils::Format("Skb priority (%d) should equal tc "
      "(%lu)!\n", tx_meta->skb_priority, tx_meta->sch_meta.tc);
  }

  /* Loom: TODO: Better error checking (and security?) on the incoming traffic
   * class. */
  assert(dataq->next_tc < SN_MAX_TC); /* TC is used as an index! It should not allow for overflow. */

  /* TODO: Set tx_meta as pkt metadata. */

  /* Metadata: Process checksumming */
  //if (tx_meta->csum_start != SN_TX_CSUM_DONT) {
  //  //do_ip_csum(pkt, tx_meta->csum_start, tx_meta->csum_dest);
  //  do_ip_tcp_csum(pkt);
  //}

  return pkt;
}

/* Loom: TODO: A reasonable way of hacking things up would be to reinterpret
 * qid in this case instead as tcid (traffic class id).  However, this should
 * probably get copy+pasted into a new LoomVPort file to avoid breaking the
 * existing VPort implementation (which I want to compare against) */
int LoomVPort::RecvPacketsDataQ(queue_t qid, bess::Packet **pkts, int max_cnt) {
  //uint64_t total_bytes;
  int cnt;
  //int i;

  if (static_cast<size_t>(max_cnt) > bess::PacketBatch::kMaxBurst) {
    max_cnt = bess::PacketBatch::kMaxBurst;
  }

  /* Loom: TODO: Make the different qids (inc_qs_) have different sets of
   * scheduling queues. */

  /* Loom: DEBUG: lets just test up to here for now... */
  /* Try to read a batch of packets. */
  if (sch_hier_ == SCH_DRR) {
    cnt = GetNextDrrBatch(pkts, max_cnt);
  } else {
    cnt = GetNextPifoBatch(pkts, max_cnt);
  }

  /* Loom: TODO: I'm not sure whether I should do this or not. Right now I'm
   * thinking no because it breaks the ability to configure the scheduler for
   * how often it polls the control descriptors. */
  /* If there are no packets to be read, see if reading control descriptors
   * notifies us of new packets. */
  //if (cnt == 0) {
  //  RecvCtrlDesc();
  //  cnt = GetNextPifoBatch(pkts, max_cnt);
  //  cnt = GetNextDrrBatch(pkts, max_cnt);
  //}

  /* LOOM: DEBUG */
#if 0
  if (cnt > 0) {
    LOG(INFO) << bess::utils::Format("LoomVPort RecvPackets for ctrl q: %d. %d packets",
      qid, cnt);
  }
#endif

  /* TODO: should any more procesing be done on the packets? */
  //for (i = 0; i < cnt; i++) {
  //}

  return cnt;
}

int LoomVPort::RecvCtrlDesc() {
  struct queue *tx_ctrl_queue;
  struct sn_tx_ctrl_desc ctrl_desc_arr[bess::PacketBatch::kMaxBurst];
  int ctrl_desc_cnt;
  /* Loom: TODO: what batch size? */
  int max_cnt = bess::PacketBatch::kMaxBurst; 
  int refill_cnt;
  int qid;
  int ret;
  int cnt = 0;

  /* XXX: Hack for debugging performance. This would be better off configured
   * through bessctl traffic classes (e.g., weighted share round robin) */
  //ctrl_desc_call_i_++;
  //ctrl_desc_call_i_ = ctrl_desc_call_i_ % 4;
  //if (ctrl_desc_call_i_ != 0) {
  //  return cnt;
  //}

  for (qid = 0; qid < num_tx_ctrlqs_; qid++) {
    tx_ctrl_queue = &inc_ctrl_qs_[qid];

    /* Process ctrl desriptors */
    /* Loom: TODO: different max_cnt? */
    /* Loom: TODO: Poll all ctrl queues? */
    ctrl_desc_cnt = DequeueCtrlDescs(tx_ctrl_queue, ctrl_desc_arr, max_cnt);
    cnt += ctrl_desc_cnt;

    ret = ProcessCtrlDescs(ctrl_desc_arr, ctrl_desc_cnt);
    if (ret != 0) {
      LOG(ERROR) << "Unexpected error in ProcessCtrlDescs";
    }

    /* Send more buffers to the driver. */
    refill_cnt = refill_tx_bufs(tx_ctrl_queue->sn_to_drv);
    refill_cnt = refill_cnt; /* XXX: avoid warnings. */

    /* If the driver is requesting a TX interrupt, generate one. */
    if (__sync_bool_compare_and_swap(&tx_ctrl_queue->tx_regs->irq_disabled, 0, 1)) {

      /* TODO: trigger interrupts for specific queues.  The major question is on
       * which cores should napi_schedule be called from. */
      /* TODO: this would be better done with queues instead of cores.  However,
       * the SN kmod should still then be responsible for kicking the interrupt
       * on the appropriate core. */
      /* TODO: In addition to a cpu_to_txq queue mapping, we should also maintain
       * a txq_to_cpu mapping to make this part better. */
      uint64_t cpu = 0;
      uint64_t _cpui;
      for (_cpui = 0; _cpui < SN_MAX_CPU; _cpui++) {
        if (map_.cpu_to_tx_ctrlq[_cpui] == qid) {
          cpu = _cpui;
          break;
        }
      }
      uint64_t cpumask = (1ull << cpu);

      //LOG(INFO) << bess::utils::Format("ioctl(KICK_TX) for cpu: %d, txq: %d, cpumask: %x",
      //    cpu, qid, cpumask);

      ret = ioctl(fd_, SN_IOC_KICK_TX, cpumask);
      if (ret) {
        PLOG(ERROR) << "ioctl(KICK_TX)";
      }
    }
  }

  return cnt;
}

int LoomVPort::SendPackets(queue_t qid, bess::Packet **pkts, int cnt) {
  struct queue *rx_queue = &out_qs_[qid];

  phys_addr_t paddr[bess::PacketBatch::kMaxBurst];

  int ret;

  assert(static_cast<size_t>(cnt) <= bess::PacketBatch::kMaxBurst);

  reclaim_packets(rx_queue->drv_to_sn);

  //LOG(INFO) << "qid: " << (int)qid << " receiving " << cnt << " packets";

  for (int i = 0; i < cnt; i++) {
    bess::Packet *snb = pkts[i];

    struct sn_rx_desc *rx_desc;

    rx_desc = snb->scratchpad<struct sn_rx_desc *>();

    rte_prefetch0(rx_desc);

    paddr[i] = snb->paddr();
  }

  for (int i = 0; i < cnt; i++) {
    bess::Packet *snb = pkts[i];
    bess::Packet *seg;

    /* Loom: DEBUG */
#if 0
    if (snb->nb_segs() > 1) {
      LOG(INFO) << "(Port " << name() << ") Number of segs: " << snb->nb_segs();
    }
    if (snb->total_len() > 1550) {
      LOG(INFO) << "(Port " << name() << ") Packet len > 1550 (" << snb->total_len() << ")";
    }
    if (!snb->is_linear()) {
      LOG(INFO) << "(Port " << name() << ") Packet is not linear!";
    }
    if (!snb->is_simple()) {
      LOG(INFO) << "(Port " << name() << ") Packet is not simple!";
    }
#endif
    if (snb->nb_segs() > 1 || snb->total_len() > 1550 ||
        !snb->is_linear() || !snb->is_simple()) {
      LOG(INFO) << "(Port " << name() << ") Dropping sn_to_drv (Outgoing/Kernel RX) packet!";
    }

    struct sn_rx_desc *rx_desc;

    rx_desc = snb->scratchpad<struct sn_rx_desc *>();

    rx_desc->total_len = snb->total_len();
    rx_desc->seg_len = snb->head_len();
    rx_desc->seg = snb->dma_addr();
    rx_desc->next = 0;

    rx_desc->meta = sn_rx_metadata();

    seg = reinterpret_cast<bess::Packet *>(snb->next());
    while (seg) {
      struct sn_rx_desc *next_desc;
      bess::Packet *seg_snb;

      seg_snb = (bess::Packet *)seg;
      next_desc = seg_snb->scratchpad<struct sn_rx_desc *>();

      next_desc->seg_len = seg->head_len();
      next_desc->seg = seg->dma_addr();
      next_desc->next = 0;

      rx_desc->next = seg_snb->paddr();
      rx_desc = next_desc;
      /* LOOM: This line of code concerns me.  Shouldn't it be seg->next()
       * instead of snb->next()? */
      seg = reinterpret_cast<bess::Packet *>(seg->next());
    }
  }

  ret = llring_mp_enqueue_bulk(rx_queue->sn_to_drv, paddr, cnt);

  if (ret == -LLRING_ERR_NOBUF)
    return 0;

  /* TODO: generic notification architecture */
  if (__sync_bool_compare_and_swap(&rx_queue->rx_regs->irq_disabled, 0, 1)) {
    uint64_t cpumask = (1ull << map_.rxq_to_cpu[qid]);

    //LOG(INFO) << bess::utils::Format("ioctl(KICK_RX) for cpu: %d, rxq: %d, cpumask: %x",
    //    map_.rxq_to_cpu[qid], qid, cpumask);

    ret = ioctl(fd_, SN_IOC_KICK_RX, cpumask);
    //LOG(INFO) << "ioctl(KICK_RX)";
    if (ret) {
      PLOG(ERROR) << "ioctl(KICK_RX)";
    }
  }

  return cnt;
}

ADD_DRIVER(LoomVPort, "loom_vport", "Loom Virtual port for Linux host")
