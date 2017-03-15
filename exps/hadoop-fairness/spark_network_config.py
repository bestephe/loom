#!/usr/bin/python

import argparse
import glob
import os
import platform
import subprocess
import sys
import yaml
from time import sleep

QMODEL_SQ = 'sq'
QMODEL_MQ = 'mq'

SPARK_CONFIG_DEFAULTS = {
    'qmodel': QMODEL_SQ,
    'iface': 'eno2',
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

class SparkConfig(object):
    def __init__(self, *initial_data, **kwargs):
        for key in SPARK_CONFIG_DEFAULTS:
            setattr(self, key, SPARK_CONFIG_DEFAULTS[key])
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

def spark_config_nic_driver(config):
    # Get the current IP
    get_ip_cmd='/sbin/ifconfig %s | grep \'inet addr:\' | cut -d: -f2 | awk \'{ print $1}\'' % config.iface
    ip = subprocess.check_output(get_ip_cmd, shell=True)
    ip = ip.strip()

    # Remove the driver
    rm_cmd = 'sudo rmmod ixgbeloom'
    os.system(rm_cmd) # Ignore everything

    # Craft the args for the driver
    ixgbe = '../../code/ixgbe-5.0.4/src/ixgbeloom.ko'
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

def get_txqs(args):
    txqs = glob.glob('/sys/class/net/%s/queues/tx-*' % args.iface)
    return txqs

def get_rxqs(config):
    rxqs = glob.glob('/sys/class/net/%s/queues/rx-*' % config.iface)
    return rxqs 

def spark_configure_rfs(config):
    rxqs = get_rxqs(config)
    entries = 65536
    entries_per_rxq = entries / len(rxqs)
    cmd = 'echo %d | sudo tee /proc/sys/net/core/rps_sock_flow_entries > /dev/null' % \
        entries
    subprocess.check_call(cmd, shell=True)
    for rxq in rxqs:
        cmd = 'echo %d | sudo tee /%s/rps_flow_cnt > /dev/null' % (entries_per_rxq, rxq)
        subprocess.check_call(cmd, shell=True)

def spark_config_xps(config):
    if config.qmodel == QMODEL_MQ:
        # Use the Intel script to configure XPS
        subprocess.call('sudo killall irqbalance', shell=True)
        xps_script = '../../code/ixgbe-5.0.4/scripts/set_irq_affinity'
        subprocess.call('sudo %s -x local %s' % (xps_script, config.iface),
            shell=True)

        # Also configure RFS
        spark_configure_rfs(config)

def spark_config_qdisc(config):
    #XXX: DEBUG:
    #print 'WARNING: Skipping Qdisc config!'
    #return

    qcnt = len(get_txqs(config))
    for i in xrange(1, qcnt + 1):
        # Configure a DRR Qdisc per txq
        tc_cmd = 'sudo tc qdisc add dev %s parent :%x handle %d00: drr' % \
            (config.iface, i, i)
        subprocess.check_call(tc_cmd, shell=True)
        tc_cmd = 'sudo tc class add dev %s parent %d00: classid %d00:1 drr' % \
            (config.iface, i, i)
        subprocess.check_call(tc_cmd, shell=True)
        tc_cmd = 'sudo tc class add dev %s parent %d00: classid %d00:2 drr' % \
            (config.iface, i, i)
        subprocess.check_call(tc_cmd, shell=True)
        # Note: I don't know if I need to add a child to each DRR class, but it
        # seems like a reasonable idea.  I could just add pfifo_fast, but
        # adding SFQ also makes some sense.
        tc_cmd = 'sudo tc qdisc add dev %s parent %d00:1 sfq limit 32768 perturb 60' % \
            (config.iface, i)
        subprocess.check_call(tc_cmd, shell=True)
        tc_cmd = 'sudo tc qdisc add dev %s parent %d00:2 sfq limit 32768 perturb 60' % \
            (config.iface, i)
        subprocess.check_call(tc_cmd, shell=True)

        # Create traffic filters to send traffic from the second spark
        # instance (ubuntu2) to :2
        h2_ports = [9077, 9080, 9091, 9092, 9093, 9094, 9095, 9096, 9097, 9098, 9099, 9337]
        for p in h2_ports:
            for pdir in ['sport', 'dport']:
                tc_str = 'sudo tc filter add dev %s protocol ip parent %d00: ' + \
                    'prio 1 u32 match ip %s %d 0xffff flowid %d00:2'
                tc_cmd = tc_str % (config.iface, i, pdir, p, i)
                subprocess.check_call(tc_cmd, shell=True)
        for pdir in ['sport', 'dport']:
            tc_str = 'sudo tc filter add dev %s protocol ip parent %d00: ' + \
                'prio 1 u32 match ip %s 32768 0xff00 flowid %d00:2' 
            tc_cmd = tc_str % (config.iface, i, pdir, i)
            subprocess.check_call(tc_cmd, shell=True)

        # Create a traffic filter to send the rest of the traffic to class :1
        tc_str = 'sudo tc filter add dev %s protocol all parent %d00: ' + \
            'prio 2 u32 match ip dst 0.0.0.0/0 flowid %d00:1'
        tc_cmd = tc_str % (config.iface, i, i)
        subprocess.check_call(tc_cmd, shell=True)

def spark_config_server(config):
    # Configure the number of NIC queues
    spark_config_nic_driver(config)

    # Configure XPS
    spark_config_xps(config)

    # Configure Qdisc/TC
    spark_config_qdisc(config)

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
        config = SparkConfig(user_config)
    else:
        config = SparkConfig()

    # Configure the server
    spark_config_server(config)

if __name__ == '__main__':
    main()
