#!/usr/bin/python

import argparse
import glob
import os
import platform
import subprocess
import sys
import yaml
from time import sleep

DRIVER_DIR = '/proj/opennf-PG0/exp/Loom-Terasort/datastore/git/loom-code/code/ixgbe-5.0.4/'
TCP_BYTE_LIMIT_DIR = '/proc/sys/net/ipv4/tcp_limit_output_bytes'
TCP_QUEUE_SYSTEM_DEFAULT = 262144

QMODEL_SQ = 'sq'
QMODEL_MQ = 'mq'

PRI_CONFIG_DEFAULTS = {
    'qmodel': QMODEL_SQ,

    'iface': 'eno2',
    'bql_limit_max': (256 * 1024),
    'smallq_size': TCP_QUEUE_SYSTEM_DEFAULT,
}

class ServerConfig(object):
    def __init__(self, *initial_data, **kwargs):
        for dictionary in initial_data:
            for key in dictionary:
                setattr(self, key, dictionary[key])
        for key in kwargs:
            setattr(self, key, kwargs[key])
    def dump(self):
        return self.__dict__.copy()

class PriConfig(object):
    def __init__(self, *initial_data, **kwargs):
        for key in PRI_CONFIG_DEFAULTS:
            setattr(self, key, PRI_CONFIG_DEFAULTS[key])
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
        d = self.__dict__.copy()
        return d

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

def get_queue_bql_limit_max(config, txqi):
    sysfs_dir = '/sys/class/net/%s' % config.iface
    limitfname = sysfs_dir + '/queues/tx-%d/byte_queue_limits/limit_max' % txqi
    with open(limitfname) as limitf:
        limit = limitf.read()
    limit.strip()
    limit = int(limit)
    return limit

def set_queue_bql_limit_max(config, txqi, limit):
    sysfs_dir = '/sys/class/net/%s' % config.iface
    limitfname = sysfs_dir + '/queues/tx-%d/byte_queue_limits/limit_max' % txqi
    with open(limitfname, 'w') as limitf:
        limitf.write('%d\n' % limit)
    assert (get_queue_bql_limit_max(config, txqi) == limit)

def get_queue_bql_limit_min(config, txqi):
    sysfs_dir = '/sys/class/net/%s' % config.iface
    limitfname = sysfs_dir + '/queues/tx-%d/byte_queue_limits/limit_min' % txqi
    with open(limitfname) as limitf:
        limit = limitf.read()
    limit.strip()
    limit = int(limit)
    return limit

def set_queue_bql_limit_min(config, txqi, limit):
    sysfs_dir = '/sys/class/net/%s' % config.iface
    limitfname = sysfs_dir + '/queues/tx-%d/byte_queue_limits/limit_min' % txqi
    with open(limitfname, 'w') as limitf:
        limitf.write('%d\n' % limit)
    assert (get_queue_bql_limit_min(config, txqi) == limit)

def set_all_bql_limit_max(config):
    for txqi in range(len(get_txqs(config))):
        set_queue_bql_limit_max(config, txqi, config.bql_limit_max)
        set_queue_bql_limit_min(config, txqi, 0)


#
# The rest of the functions
#
def pri_config_nic_driver(config):
    # Get the current IP
    get_ip_cmd='/sbin/ifconfig %s | grep \'inet addr:\' | cut -d: -f2 | awk \'{ print $1}\'' % config.iface
    ip = subprocess.check_output(get_ip_cmd, shell=True)
    ip = ip.strip()

    # Remove the driver
    rm_cmd = 'sudo rmmod ixgbeloom'
    os.system(rm_cmd) # Ignore everything

    # Craft the args for the driver
    ixgbe = DRIVER_DIR + '/src/ixgbeloom.ko'
    rss_str = '' if config.qmodel == QMODEL_MQ else 'RSS=1,1'
    drv_cmd = 'sudo insmod %s %s' % (ixgbe, rss_str)

    # Add in the new driver
    subprocess.check_call(drv_cmd, shell=True)

    # Unbind the module from ixgbe always even if not present and bind to ixgbetitan
    cmd = "sudo /bin/su -c \"echo -n '0000:81:00.1' > /sys/bus/pci/drivers/ixgbe/unbind\""
    os.system(cmd)
    cmd = "sudo /bin/su -c \"echo -n '0000:81:00.1' > /sys/bus/pci/drivers/ixgbeloom/bind\""
    os.system(cmd)

    # Assign the IP
    ip_cmd = 'sudo ifconfig %s %s netmask 255.255.255.0' % (config.iface, ip)
    subprocess.check_call(ip_cmd, shell=True)

def get_txqs(config):
    txqs = glob.glob('/sys/class/net/%s/queues/tx-*' % config.iface)
    return txqs

def get_rxqs(config):
    rxqs = glob.glob('/sys/class/net/%s/queues/rx-*' % config.iface)
    return rxqs 

def pri_configure_rfs(config):
    rxqs = get_rxqs(config)
    entries = 65536
    entries_per_rxq = entries / len(rxqs)
    cmd = 'echo %d | sudo tee /proc/sys/net/core/rps_sock_flow_entries > /dev/null' % \
        entries
    subprocess.check_call(cmd, shell=True)
    for rxq in rxqs:
        cmd = 'echo %d | sudo tee /%s/rps_flow_cnt > /dev/null' % (entries_per_rxq, rxq)
        subprocess.check_call(cmd, shell=True)

def pri_config_xps(config):
    if config.qmodel == QMODEL_MQ:
        # Use the Intel script to configure XPS
        subprocess.call('sudo killall irqbalance', shell=True)
        xps_script = DRIVER_DIR + '/scripts/set_irq_affinity'
        subprocess.call('sudo %s -x all %s' % (xps_script, config.iface),
            shell=True)

        # Also configure RFS
        pri_configure_rfs(config)
    else:
        #Note: maybe not necessary.  But it shouldn't hurt to restart irqbalance
        subprocess.call('sudo service irqbalance restart', shell=True)

def pri_config_qdisc(config):
    #XXX: DEBUG:
    #print 'WARNING: Skipping Qdisc config!'
    #return

    qcnt = len(get_txqs(config))
    for i in xrange(1, qcnt + 1):
        # Configure a prio Qdisc per txq with SFQ children
        tc_cmd = 'sudo tc qdisc add dev %s parent :%x handle %d00: prio' % \
            (config.iface, i, i)
        subprocess.check_call(tc_cmd, shell=True)
        tc_cmd = 'sudo tc qdisc add dev %s parent %d00:1 sfq limit 32768 perturb 60' % \
            (config.iface, i)
        subprocess.check_call(tc_cmd, shell=True)
        tc_cmd = 'sudo tc qdisc add dev %s parent %d00:2 sfq limit 32768 perturb 60' % \
            (config.iface, i)
        subprocess.check_call(tc_cmd, shell=True)
        tc_cmd = 'sudo tc qdisc add dev %s parent %d00:3 sfq limit 32768 perturb 60' % \
            (config.iface, i)
        subprocess.check_call(tc_cmd, shell=True)

        # Create a filter for memcached traffic
        for pdir in ['sport', 'dport']:
            tc_str = 'sudo tc filter add dev %s protocol ip parent %d00: ' + \
                'prio 1 u32 match ip %s %d 0xffff flowid %d00:1'
            tc_cmd = tc_str % (config.iface, i, pdir, 11212, i)
            subprocess.check_call(tc_cmd, shell=True)

        # Create a traffic filter to send the rest of the traffic to priority :2
        tc_str = 'sudo tc filter add dev %s protocol all parent %d00: ' + \
            'prio 2 u32 match ip dst 0.0.0.0/0 flowid %d00:2'
        tc_cmd = tc_str % (config.iface, i, i)
        subprocess.check_call(tc_cmd, shell=True)

def pri_config_server(config):
    # Configure the number of NIC queues
    pri_config_nic_driver(config)

    # Configure XPS
    pri_config_xps(config)

    # Configure Qdisc/TC
    pri_config_qdisc(config)

    # Configure BQL
    set_all_bql_limit_max(config)

def main():
    # Parse arguments
    parser = argparse.ArgumentParser(description='Configure the server and '
        'start spark for the fairness experiment')
    parser.add_argument('--config', help='An alternate configuration file. '
        'The configuration format is unsurprisingly not documented.')
    args = parser.parse_args()

    # Get the config
    if args.config:
        with open(args.config) as configf:
            user_config = yaml.load(configf)
        config = PriConfig(user_config)
    else:
        config = PriConfig()

    # Configure the server
    pri_config_server(config)

if __name__ == '__main__':
    main()
