#!/usr/bin/python

import argparse
import glob
import os
import platform
import subprocess
import yaml

VM_CONFIG_DEFAULTS = {
    'iface': 'ens4',
    'ip': '10.10.1.3',
    'netmask': '255.255.255.0',
}

class VmConfig(object):
    def __init__(self, *initial_data, **kwargs):
        for key in VM_CONFIG_DEFAULTS:
            setattr(self, key, VM_CONFIG_DEFAULTS[key])
        for dictionary in initial_data:
            for key in dictionary:
                if not hasattr(self, key):
                    print 'WARNING! Unexpected attr: %s' % key
                setattr(self, key, dictionary[key])
        for key in kwargs:
            if not hasattr(self, key):
                print 'WARNING! Unexpected attr: %s' % key
            setattr(self, key, kwargs[key])
    def dump(self):
        return self.__dict__.copy()

def vm_config_loom_iface(config):
    if_cmd_str = 'sudo ifconfig %(iface)s %(ip)s netmask %(netmask)s'
    if_cmd_args = {'iface': config.iface, 'ip': config.ip,
        'netmask': config.netmask}
    if_cmd = if_cmd_str % if_cmd_args
    subprocess.check_call(if_cmd, shell=True)

def vm_config_loom_tc(config):
    qnums = get_iface_qnums(config)
    print qnums
    for i in qnums:
        tc_cmd = 'sudo tc qdisc add dev %s parent :%x loom' % \
            (config.iface, i + 1) # NOTE: +1!
        subprocess.check_call(tc_cmd, shell=True)

def vm_config_loom(config):
    # Configure the interface
    vm_config_loom_iface(config)

    # Configure TC
    vm_config_loom_tc(config)

def get_iface_qnums(config):
    qs = glob.glob('/sys/class/net/%s/queues/tx-*' % config.iface)
    def qnum_from_qdir(qdir):
        s = qdir.split('/')[-1]
        s = s.split('-')[-1]
        return int(s)
    return [qnum_from_qdir(q) for q in qs]

def main():
    # Parse arguments
    parser = argparse.ArgumentParser(description='Configure the VM for the '
        'Loom Qdisc')
    parser.add_argument('--config', help='An alternate configuration file. '
        'The configuration format is unsurprisingly not documented.')
    args = parser.parse_args()

    # Get the config
    if args.config:
        with open(args.config) as configf:
            user_config = yaml.load(configf)
        config = VmConfig(user_config)
    else:
        config = VmConfig()

    # Configure the VM
    vm_config_loom(config)

if __name__ == '__main__':
    main()
