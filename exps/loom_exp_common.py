#!/usr/bin/python

import os
import subprocess
import yaml

if 'LOOM_HOME' in os.environ:
    LOOM_HOME = os.environ['LOOM_HOME']
else:
    LOOM_HOME = '/proj/opennf-PG0/exp/Loom-Terasort/datastore/git/loom-code/'
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
    hp_cmd = 'sudo sysctl vm.nr_hugepages=1024'
    subprocess.check_call(hp_cmd, shell=True)

    # Start BESS with the appropriate configuration
    bess_conf = os.path.abspath(config.bess_conf)
    bess_cmd = 'sudo bessctl/bessctl -- daemon start -- run file %s' % bess_conf
    subprocess.check_call(bess_cmd, shell=True, cwd=BESS_HOME)


def loom_config_bess(config):
    loom_config_bess_kmod(config)
    loom_config_dpdk(config)
