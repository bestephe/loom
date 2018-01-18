#!/usr/bin/python

import glob
import os
import shlex
import subprocess
import yaml

if 'LOOM_HOME' in os.environ:
    LOOM_HOME = os.environ['LOOM_HOME']
else:
    LOOM_HOME = '/proj/opennf-PG0/exp/loomtest2/datastore/bes/git/loom-code/'
BESS_HOME = LOOM_HOME + '/code/bess/'

#
# TCP SmallQs and BQL Functions
#
def get_tcp_limit():
    with open(TCP_BYTE_LIMIT_DIR) as tcpf:
        limit = tcpf.read()
    limit.strip()
    limit = int(limit)
    return limit

def set_tcp_limit(b):
    with open(TCP_BYTE_LIMIT_DIR, 'w') as tcpf:
        tcpf.write('%d\n' % b)
    assert (get_tcp_limit() == b)

def get_queue_bql_limit_max(config, iface, txqi):
    sysfs_dir = '/sys/class/net/%s' % iface
    limitfname = sysfs_dir + '/queues/tx-%d/byte_queue_limits/limit_max' % txqi
    with open(limitfname) as limitf:
        limit = limitf.read()
    limit.strip()
    limit = int(limit)
    return limit

def set_queue_bql_limit_max(config, iface, txqi, limit):
    sysfs_dir = '/sys/class/net/%s' % iface
    limitfname = sysfs_dir + '/queues/tx-%d/byte_queue_limits/limit_max' % txqi
    with open(limitfname, 'w') as limitf:
        limitf.write('%d\n' % limit)
    assert (get_queue_bql_limit_max(config, iface, txqi) == limit)

def get_queue_bql_limit_min(config, iface, txqi):
    sysfs_dir = '/sys/class/net/%s' % iface
    limitfname = sysfs_dir + '/queues/tx-%d/byte_queue_limits/limit_min' % txqi
    with open(limitfname) as limitf:
        limit = limitf.read()
    limit.strip()
    limit = int(limit)
    return limit

def set_queue_bql_limit_min(config, iface, txqi, limit):
    sysfs_dir = '/sys/class/net/%s' % iface
    limitfname = sysfs_dir + '/queues/tx-%d/byte_queue_limits/limit_min' % txqi
    with open(limitfname, 'w') as limitf:
        limitf.write('%d\n' % limit)
    assert (get_queue_bql_limit_min(config, iface, txqi) == limit)

def set_all_bql_limit_max(config, iface):
    for txqi in range(len(get_txqs(iface))):
        set_queue_bql_limit_max(config, iface, txqi, config.bql_limit_max)
        set_queue_bql_limit_min(config, iface, txqi, 0)

#
# CGroup Helpers
# 
DEFAULT_CGCONFIG = {'high_prio': 3}
def config_cgroup(config, iface):
    cgconfig = config.cgroups if hasattr(config, 'cgroups') else DEFAULT_CGCONFIG
    
    for cgname in cgconfig:
        cgroup_dir = '/sys/fs/cgroup/net_prio/%s' % cgname
        cgroup_prio = cgconfig[cgname]

        mkdir_cmd = 'sudo mkdir %s' % cgroup_dir
        subprocess.call(mkdir_cmd, shell=True)

        priomap_cmd = 'sudo echo \"%s %d\" > %s/net_prio.ifpriomap' % \
            (iface, cgroup_prio, cgroup_dir)
        subprocess.check_call(priomap_cmd, shell=True)

        check_cgroup(config, iface, cgname)

    #XXX: Check default cgroup
    print 'Default cgroup:'
    default_cmd = 'cat /sys/fs/cgroup/net_prio/net_prio.ifpriomap'
    print subprocess.check_output(default_cmd, shell=True)

def check_cgroup(config, iface, cgname):
    print 'Cgroup: %s' % cgname
    check_prio_cmd = 'cat /sys/fs/cgroup/net_prio/%s/net_prio.ifpriomap' % cgname
    print subprocess.check_output(check_prio_cmd, shell=True)

#
# More helpers
# 
def get_txqs(iface):
    txqs = glob.glob('/sys/class/net/%s/queues/tx-*' % iface)
    return txqs

def get_rxqs(iface):
    rxqs = glob.glob('/sys/class/net/%s/queues/rx-*' % iface)
    return rxqs 

def configure_rfs(config, iface):
    rxqs = get_rxqs(iface)
    entries = 65536
    entries_per_rxq = entries / len(rxqs)
    cmd = 'echo %d | sudo tee /proc/sys/net/core/rps_sock_flow_entries > /dev/null' % \
        entries
    subprocess.check_call(cmd, shell=True)
    for rxq in rxqs:
        cmd = 'echo %d | sudo tee /%s/rps_flow_cnt > /dev/null' % (entries_per_rxq, rxq)
        subprocess.check_call(cmd, shell=True)

#
# BESS Configuration commands
#
def loom_config_bess_kmod(config):
    kmod_dir = BESS_HOME + '/core/kmod'
    kmod_cmd = 'sudo ./install'
    subprocess.check_call(kmod_cmd, shell=True, cwd=kmod_dir)

def loom_config_dpdk(config):
    modprobe_cmd = 'sudo modprobe uio_pci_generic'
    subprocess.check_call(modprobe_cmd, shell=True)

    # Try to bring down the interface 
    if_cmd = 'sudo ifconfig %s 0.0.0.0' % config.iface
    subprocess.call(if_cmd, shell=True)

    # Bind the interface for use by DPDK
    devbind_cmd = 'sudo ./bin/dpdk-devbind.py -b uio_pci_generic %s' % \
        config.iface_addr
    subprocess.check_call(devbind_cmd, shell=True, cwd=BESS_HOME)

    # Configure huge pages
    #hp_cmd = 'sudo sysctl vm.nr_hugepages=1024'
    #subprocess.check_call(hp_cmd, shell=True)

def loom_config_bess(config):
    loom_config_bess_kmod(config)
    loom_config_dpdk(config)

    # Start BESS with the appropriate configuration
    #  Note: keep bessctl running in the background to keep tcpdump from BESS working
    bessctl = os.path.abspath(config.bessctl)
    bess_cmd = 'nohup sudo bessctl/bessctl -- daemon start -- run file %s ' % \
        bessctl
    bessctl_proc = subprocess.Popen(shlex.split(bess_cmd), cwd=BESS_HOME,
        #stdout=open('/dev/null', 'w'), stderr=open('/dev/null', 'w'),
        stdout=open('/tmp/bessctl_stdout', 'w'), stderr=open('/tmp/bessctl_stderr', 'w'),
        preexec_fn=os.setpgrp)

    #TODO: depending on how PCAP file is being created, this may stall. I need
    # to revist this later.
    bessctl_proc.wait()

    return bessctl_proc
