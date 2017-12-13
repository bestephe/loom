#!/usr/bin/python

import os
import shlex
import subprocess
import yaml

if 'LOOM_HOME' in os.environ:
    LOOM_HOME = os.environ['LOOM_HOME']
else:
    LOOM_HOME = '/proj/opennf-PG0/exp/loomtest/datastore/bes/git/loom-code/'
BESS_HOME = LOOM_HOME + '/code/bess/'

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
