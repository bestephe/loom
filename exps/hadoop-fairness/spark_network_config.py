#!/usr/bin/python

import argparse
import glob
import os
import platform
import subprocess
import sys
import yaml
from time import sleep

sys.path.insert(0, os.path.abspath('..'))
from loom_exp_common import *

if 'LOOM_HOME' in os.environ:
    LOOM_HOME = os.environ['LOOM_HOME']
else:
    LOOM_HOME = '/proj/opennf-PG0/exp/loomtest40g/datastore/bes/git/loom-code/'

DRIVER_DIR = LOOM_HOME + '/code/ixgbe-5.0.4/'
TCP_BYTE_LIMIT_DIR = '/proc/sys/net/ipv4/tcp_limit_output_bytes'
TCP_QUEUE_SYSTEM_DEFAULT = 262144

QMODEL_SQ = 'sq'
QMODEL_MQ = 'mq'
QMODEL_BESS = 'bess'

H2_PORTS = [9020, 9077, 9080, 9091, 9092, 9093, 9094, 9095, 9096, 9097,
    9098, 9099, 9337, 51070, 51090, 51091, 51010, 51075, 51020, 51070,
    51475, 51470, 51100, 51105, 9485, 9480, 9481, 3049, 5242]

SPARK_CONFIG_DEFAULTS = {
    'qmodel': QMODEL_BESS,
    'job_fair_ratio': 1,

    'iface': 'eno2',
    'ifaces': ['loom1', 'loom2'],
    'iface_addr': '0000:81:00.1',
    'bessctl': 'fairness.bess',
    'bql_limit_max': (256 * 1024),
    'smallq_size': TCP_QUEUE_SYSTEM_DEFAULT,
    'qdisc': True,
    'pri': True,

    #'drr_quantum': 65536,
    'drr_quantum': 1500,
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
# The rest of the functions
#
def spark_config_nic_driver(config):
    # Get the current IP
    get_ip_cmd='/sbin/ifconfig %s | grep \'inet addr:\' | cut -d: -f2 | awk \'{ print $1}\'' % config.iface
    ip = subprocess.check_output(get_ip_cmd, shell=True)
    ip = ip.strip()

    # Remove the driver
    rm_cmd = 'sudo rmmod ixgbeloom'
    os.system(rm_cmd) # Ignore everything

    # Craft the args for the driver
    ixgbe = DRIVER_DIR + '/src/ixgbeloom.ko'
    rss_str = '' if config.qmodel == QMODEL_MQ else 'RSS=1,1 MQ=0,0'
    drv_cmd = 'sudo insmod %s %s' % (ixgbe, rss_str)

    # Add in the new driver
    subprocess.check_call(drv_cmd, shell=True)

    # Unbind the module from ixgbe always even if not present and bind to ixgbeloom
    cmd = "sudo /bin/su -c \"echo -n '0000:81:00.1' > /sys/bus/pci/drivers/ixgbe/unbind\""
    os.system(cmd)
    cmd = "sudo /bin/su -c \"echo -n '0000:81:00.1' > /sys/bus/pci/drivers/ixgbeloom/bind\""
    os.system(cmd)

    # Assign the IP
    ip_cmd = 'sudo ifconfig %s %s netmask 255.255.255.0' % (config.iface, ip)
    subprocess.check_call(ip_cmd, shell=True)

    # Assign additional IPs
    for ip_part in ['101', '102']:
        ip_split = ip.split('.')
        ip_split[2] = ip_part
        ip_extra = '.'.join(ip_split)
        ip_cmd = 'sudo ip addr add %s/24 dev %s' % (ip_extra, config.iface)
        subprocess.check_call(ip_cmd, shell=True)

def get_txqs(iface):
    txqs = glob.glob('/sys/class/net/%s/queues/tx-*' % iface)
    return txqs

def get_rxqs(iface):
    rxqs = glob.glob('/sys/class/net/%s/queues/rx-*' % iface)
    return rxqs 

def spark_configure_rfs(config, iface):
    rxqs = get_rxqs(iface)
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
        xps_script = DRIVER_DIR + '/scripts/set_irq_affinity'
        subprocess.call('sudo %s -x all %s' % (xps_script, config.iface),
            shell=True)

        # Also configure RFS
        spark_configure_rfs(config, config.iface)
    else:
        #Note: maybe not necessary.  But it shouldn't hurt to restart irqbalance
        subprocess.call('sudo service irqbalance restart', shell=True)

def spark_config_qdisc(config, iface):
    #XXX: DEBUG:
    #print 'WARNING: Skipping Qdisc config!'
    #return

    qcnt = len(get_txqs(iface))
    for i in xrange(1, qcnt + 1):

        if config.pri:
            # Configure a Pri Qdisc per txq
            try:
                tc_cmd = 'sudo tc qdisc add dev %s parent :%x handle %d00: prio' % \
                    (iface, i, i)
                subprocess.check_call(tc_cmd, shell=True)
            except subprocess.CalledProcessError:
                tc_cmd = 'sudo tc qdisc add dev %s root handle %d00: prio' % \
                    (iface, i)
                subprocess.check_call(tc_cmd, shell=True)
        else:
            # Configure a DRR Qdisc per txq
            try:
                tc_cmd = 'sudo tc qdisc add dev %s parent :%x handle %d00: drr' % \
                    (iface, i, i)
                subprocess.check_call(tc_cmd, shell=True)
            except subprocess.CalledProcessError:
                tc_cmd = 'sudo tc qdisc add dev %s root handle %d00: drr' % \
                    (iface, i)
                subprocess.check_call(tc_cmd, shell=True)

                # Create the classes for Job1 (%d00:1) and Job2 (%d00:2)
                tc_cmd = 'sudo tc class add dev %s parent %d00: classid %d00:1 drr quantum %d' % \
                    (iface, i, i, config.drr_quantum)
                subprocess.check_call(tc_cmd, shell=True)
                job2_quantum = int(1.0 * config.drr_quantum / config.job_fair_ratio)
                tc_cmd = 'sudo tc class add dev %s parent %d00: classid %d00:2 drr quantum %d' % \
                    (iface, i, i, job2_quantum)
                subprocess.check_call(tc_cmd, shell=True)

        # Note: I don't know if I need to add a child to each DRR class, but it
        # seems like a reasonable idea.  I could just add pfifo_fast, but
        # adding SFQ also makes some sense.
        tc_cmd = 'sudo tc qdisc add dev %s parent %d00:1 sfq limit 32768 perturb 60' % \
            (iface, i)
        subprocess.check_call(tc_cmd, shell=True)
        tc_cmd = 'sudo tc qdisc add dev %s parent %d00:2 sfq limit 32768 perturb 60' % \
            (iface, i)
        subprocess.check_call(tc_cmd, shell=True)

        # Create traffic filters to send traffic from the second spark
        # instance (ubuntu2) to :2
        for p in H2_PORTS:
            for pdir in ['sport', 'dport']:
                tc_str = 'sudo tc filter add dev %s protocol ip parent %d00: ' + \
                    'prio 1 u32 match ip %s %d 0xffff flowid %d00:2'
                tc_cmd = tc_str % (iface, i, pdir, p, i)
                subprocess.check_call(tc_cmd, shell=True)
        for pdir in ['sport', 'dport']:
            tc_str = 'sudo tc filter add dev %s protocol ip parent %d00: ' + \
                'prio 1 u32 match ip %s 32768 0xff00 flowid %d00:2' 
            tc_cmd = tc_str % (iface, i, pdir, i)
            subprocess.check_call(tc_cmd, shell=True)

        # Create a traffic filter to send the rest of the traffic to class :1
        tc_str = 'sudo tc filter add dev %s protocol all parent %d00: ' + \
            'prio 2 u32 match ip dst 0.0.0.0/0 flowid %d00:1'
        tc_cmd = tc_str % (iface, i, i)
        subprocess.check_call(tc_cmd, shell=True)

def spark_config_cgrules(config):
    if config.pri:
        classes = [('ubuntu', 'tc0'), ('ubuntu2', 'tc1')]
    else:
        classes = [('ubuntu', 'tc1'), ('ubuntu2', 'tc3')]

    for user, net_cgroup in classes:
        # Config cgrules so that all spark traffic from ubuntu uses high_prio
        cgrule_cmd = 'echo \'%s net_prio %s\' | sudo tee -a /etc/cgrules.conf' % \
            (user, net_cgroup)
        subprocess.check_call(cgrule_cmd, shell=True)

        # Kill and restart cgrulesengd
        cg_cmd = 'sudo killall cgrulesengd'
        subprocess.call(cg_cmd, shell=True)
        cg_cmd = 'echo "" | sudo tee /etc/cgconfig.conf'
        subprocess.check_call(cg_cmd, shell=True)
        cg_cmd = 'sudo cgrulesengd'
        subprocess.check_call(cg_cmd, shell=True)

        # Use cgclassify to configure the priority of all programs for ubuntu (Spark)
        #XXX: NOTE: may not be necessary?
        get_pids_cmd = 'ps aux | grep "^%s " | awk \'{ print $2 }\'' % user
        ubuntu_pids = subprocess.check_output(get_pids_cmd, shell=True)
        ubuntu_pids = ' '.join(ubuntu_pids.split())
        cg_cmd = 'sudo cgclassify -g net_prio:%s --cancel-sticky %s' % \
            (net_cgroup, ubuntu_pids)
        subprocess.check_call(cg_cmd, shell=True)

def spark_config_server(config):
    # Configure the number of NIC queues
    spark_config_nic_driver(config)

    # Configure XPS
    spark_config_xps(config)

    # Configure Qdisc/TC
    spark_config_qdisc(config, config.iface)

    # Configure CGroups
    config_cgroup(config, config.iface)

    # Configure CGroup rules
    spark_config_cgrules(config)

    # Configure BQL
    set_all_bql_limit_max(config, config.iface)

def spark_config_bess(config):
    subprocess.call('sudo killall tcpdump', shell=True)

    loom_config_bess(config)

    # Save a tcpdump file
    #XXX: Do to fd and processes stopping issues, running tcpdump from within
    # the BESS script seems to work best for now.
    #subprocess.call('sudo killall tcpdump', shell=True)
    #tcpdump_cmd = 'sudo tcpdump -r /tmp/pout.pcap -w /dev/shm/spark_tcp_flows.pcap -s 64'
    #tcpdump = subprocess.Popen(shlex.split(tcpdump_cmd),
    #    stdout=subprocess.PIPE, stderr=subprocess.STDOUT)

    # Do not configure XPS (for now)
    subprocess.call('sudo service irqbalance restart', shell=True)

    # Configure all of the interfaces
    #XXX: This code doesn't work for virtio/Vhost/TAP interfaces
    for iface in config.ifaces:
        # Configure Qdisc/TC
        #TODO: optionally skip Qdisc config
        if config.qdisc:
            spark_config_qdisc(config, iface)

        # Configure CGroups
        config_cgroup(config, iface)

        # Configure CGroup rules
        spark_config_cgrules(config)

        # Configure RFS
        spark_configure_rfs(config, iface)
    
        # Configure BQL
        set_all_bql_limit_max(config, iface)

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
    if config.qmodel == QMODEL_BESS:
        spark_config_bess(config)
    else:
        spark_config_server(config)

if __name__ == '__main__':
    main()
