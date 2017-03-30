#!/usr/bin/python

import argparse
import glob
import os
import platform
import re
import subprocess
import sys
import yaml
from time import sleep

DRIVER_DIR = '/proj/opennf-PG0/exp/Loom-Terasort/datastore/git/loom-code/code/ixgbe-5.0.4/'
TCP_BYTE_LIMIT_DIR = '/proc/sys/net/ipv4/tcp_limit_output_bytes'
TCP_QUEUE_SYSTEM_DEFAULT = 262144

QMODEL_SQ = 'sq'
QMODEL_MQ = 'mq'
QMODEL_MQPRI = 'mq-pri'
QMODEL_MQETS = 'mq-ets'
QMODELS = [QMODEL_SQ, QMODEL_MQ, QMODEL_MQPRI, QMODEL_MQETS]

PRI_PORTS = [11212, 11214]
H2_PORTS = [9020, 9077, 9080, 9091, 9092, 9093, 9094, 9095, 9096, 9097,
    9098, 9099, 9337, 51070, 51090, 51091, 51010, 51075, 51020, 51070,
    51475, 51470, 51100, 51105, 9485, 9480, 9481, 3049, 5242]

HIER_CONFIG_DEFAULTS = {
    'qmodel': QMODEL_SQ,

    'iface': 'eno2',
    'bql_limit_max': (256 * 1024),
    'smallq_size': TCP_QUEUE_SYSTEM_DEFAULT,

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

class HierConfig(object):
    def __init__(self, *initial_data, **kwargs):
        for key in HIER_CONFIG_DEFAULTS:
            setattr(self, key, HIER_CONFIG_DEFAULTS[key])
        for dictionary in initial_data:
            for key in dictionary:
                if not hasattr(self, key):
                    print 'WARNING! Unexpected attr: %s' % key
                setattr(self, key, dictionary[key])
        for key in kwargs:
            if not hasattr(self, key):
                print 'WARNING! Unexpected attr: %s' % key
            setattr(self, key, kwargs[key])

        # Error checking
        if self.qmodel not in QMODELS:
            raise ValueError('Unknown qmodel: \'%s\'' % self.qmodel)
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

def verify_dcb(config):
    dcb_check_cmd = 'sudo dcbtool gc %s dcb' % config.iface
    out = subprocess.check_output(dcb_check_cmd, shell=True)
    out_split = out.split('\n')
    off_match = re.match(r".*DCB State.*off.*", out_split[-2])
    on_match = re.match(r".*DCB State.*on.*", out_split[-2])
    assert(off_match != None or on_match != None)
    if config.qmodel == QMODEL_MQPRI or config.qmodel == QMODEL_MQETS:
        assert(on_match)
    else:
        assert(off_match)

#
# The rest of the functions
#
def hier_config_nic_driver(config):
    # Get the current IP
    get_ip_cmd='/sbin/ifconfig %s | grep \'inet addr:\' | cut -d: -f2 | awk \'{ print $1}\'' % config.iface
    ip = subprocess.check_output(get_ip_cmd, shell=True)
    ip = ip.strip()

    # Remove the driver
    rm_cmd = 'sudo rmmod ixgbeloom'
    os.system(rm_cmd) # Ignore everything

    # Craft the args for the driver
    ixgbe = DRIVER_DIR + '/src/ixgbeloom.ko'
    rss_str = 'RSS=1,1' if config.qmodel == QMODEL_SQ else ''
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

    # Restart lldpad because it might be killed
    lldp_cmd = 'sudo service lldpad restart'
    subprocess.check_call(lldp_cmd, shell=True)

    # Enable DCB and change the root qdisc if using MQPRI
    if config.qmodel == QMODEL_MQPRI:
        sleep(1.5) #XXX: dcbtool fails otherwise

        # Disable LLDP TLV transmission and receipt.  Probably not necessary,
        # but for some reasons my traffic class changes are being reverted
        # automatically as soon as I send traffic.  Which is a bummer.
        lldp_cmd = 'sudo lldptool set-lldp -i %s adminStatus=disabled' % config.iface
        subprocess.check_call(lldp_cmd, shell=True)

        # Try to use the Intel tools to configure DCB priorities
        dcb_cmd = 'sudo dcbtool sc %s dcb on' % config.iface
        subprocess.check_call(dcb_cmd, shell=True)
        sleep(0.5)
        #XXX: NOTE: dcbtool doesn't seem to work

        # Try the DCB Netlink approach to configuring DCB priorities
        #XXX: BUG: NOTE: None of the above dcbtool commands seem to actually
        # work for configuring priorities.  Although not intended for use with
        # Intel NICs, the mlnx_qos command internally relies on DCB Netlink to
        # configure ETS/Strict priorities, and this interface is also supported
        # by the Intel NIC.
        mlnx_cmd = 'sudo mlnx_qos -i %s  -p 0,0,1,1,2,2,3,3 -s strict,strict,strict,strict' % config.iface
        subprocess.check_call(mlnx_cmd, shell=True)

        # Configure the mqprio Qdisc
        tc_cmd = 'sudo tc qdisc replace dev %s root handle 1: mqprio hw 1 num_tc 4 map 0 1 2 3 3 3 3 3 0 1 1 1 3 3 3 3' % config.iface
        subprocess.check_call(tc_cmd, shell=True)

        # Try the DCB Netlink approach to configuring DCB priorities again.  Just because?
        #mlnx_cmd = 'sudo mlnx_qos -i %s  -p 0,0,1,1,2,2,3,3 -s strict,strict,strict,strict' % config.iface
        #subprocess.check_call(mlnx_cmd, shell=True)

        # Config cgroups net_prio for high priority network apps
        cgroup_cmd = 'sudo mkdir /sys/fs/cgroup/net_prio/high_prio'
        subprocess.call(cgroup_cmd, shell=True)
        cgroup_cmd = 'echo "%s 3" | sudo tee /sys/fs/cgroup/net_prio/high_prio/net_prio.ifpriomap' % config.iface
        subprocess.check_call(cgroup_cmd, shell=True)
    elif config.qmodel == QMODEL_MQETS:
        sleep(1.5) #XXX: dcbtool fails otherwise

        # Disable LLDP TLV transmission and receipt.  Probably not necessary,
        # but for some reasons my traffic class changes are being reverted
        # automatically as soon as I send traffic.  Which is a bummer.
        lldp_cmd = 'sudo lldptool set-lldp -i %s adminStatus=disabled' % config.iface
        subprocess.check_call(lldp_cmd, shell=True)

        # Try to use the Intel tools to configure DCB priorities
        dcb_cmd = 'sudo dcbtool sc %s dcb on' % config.iface
        subprocess.check_call(dcb_cmd, shell=True)
        sleep(0.5)
        #XXX: NOTE: dcbtool doesn't seem to work

        # Use the Mellanox tool to configure ETS
        mlnx_cmd = 'sudo mlnx_qos -i %s  -p 0,0,1,1,2,2,3,3 -s ets,ets,ets,ets' % config.iface
        subprocess.check_call(mlnx_cmd, shell=True)

        # Configure the mqprio Qdisc
        tc_cmd = 'sudo tc qdisc replace dev %s root handle 1: mqprio hw 1 num_tc 4 map 0 1 2 3 3 3 3 3 0 1 1 1 3 3 3 3' % config.iface
        subprocess.check_call(tc_cmd, shell=True)
        #XXX: Configuring sub-qdiscs for priority would be appropriate to
        # prioritize memcached, but this currently seems to be broken.

        # Config cgroups net_prio for high priority network apps
        cgroup_cmd = 'sudo mkdir /sys/fs/cgroup/net_prio/high_prio'
        subprocess.call(cgroup_cmd, shell=True)
        cgroup_cmd = 'echo "%s 3" | sudo tee /sys/fs/cgroup/net_prio/high_prio/net_prio.ifpriomap' % config.iface
        subprocess.check_call(cgroup_cmd, shell=True)

        # Config cgrules so that all spark traffic from ubuntu uses high_prio
        cgrule_cmd = 'echo \'ubuntu net_prio high_prio\' | sudo tee /etc/cgrules.conf'
        subprocess.check_call(cgrule_cmd, shell=True)

        # Kill and restart cgrulesengd
        cg_cmd = 'sudo killall cgrulesengd'
        subprocess.check_call(cg_cmd, shell=True)
        cg_cmd = 'echo "" | sudo tee /etc/cgconfig.conf'
        subprocess.check_call(cg_cmd, shell=True)
        cg_cmd = 'sudo cgrulesengd'
        subprocess.check_call(cg_cmd, shell=True)

        # Use cgclassify to configure the priority of all programs for ubuntu (Spark)
        #XXX: NOTE: may not be necessary?
        get_pids_cmd = 'ps aux | grep "^ubuntu " | awk \'{ print $2 }\''
        ubuntu_pids = subprocess.check_output(get_pids_cmd, shell=True)
        ubuntu_pids = ' '.join(ubuntu_pids.split())
        cg_cmd = 'sudo cgclassify -g net_prio:high_prio --cancel-sticky %s' % ubuntu_pids
        subprocess.check_call(cg_cmd, shell=True)

    else:
        # Disable any DCB traffic classes/priorities in case they have been
        # previously enabled
        sleep(1.5) #XXX: dcbtool fails otherwise
        dcb_cmd = 'sudo dcbtool sc %s dcb off' % config.iface
        subprocess.check_call(dcb_cmd, shell=True)

        # Disable cgrules
        cgrule_cmd = 'sudo rm /etc/cgrules.conf'
        subprocess.call(cgrule_cmd, shell=True)
        cg_cmd = 'sudo rm /etc/cgconfig.conf'
        subprocess.call(cg_cmd, shell=True)
        cg_cmd = 'sudo killall cgrulesengd'
        subprocess.check_call(cg_cmd, shell=True)

        # TODO: Reclassify the ubuntu programs with cgclassify


    # Verify that DCB is configured properly
    sleep(1.5) #XXX: dcbtool fails otherwise
    verify_dcb(config)

    # Kill lldpad because for some reason it seems to reconfigure the NIC
    # into a DCB-off state automatically
    lldp_cmd = 'sudo service lldpad stop'
    subprocess.check_call(lldp_cmd, shell=True)

def get_txqs(config):
    txqs = glob.glob('/sys/class/net/%s/queues/tx-*' % config.iface)
    return txqs

def get_rxqs(config):
    rxqs = glob.glob('/sys/class/net/%s/queues/rx-*' % config.iface)
    return rxqs 

def hier_configure_rfs(config):
    rxqs = get_rxqs(config)
    entries = 65536
    entries_per_rxq = entries / len(rxqs)
    cmd = 'echo %d | sudo tee /proc/sys/net/core/rps_sock_flow_entries > /dev/null' % \
        entries
    subprocess.check_call(cmd, shell=True)
    for rxq in rxqs:
        cmd = 'echo %d | sudo tee /%s/rps_flow_cnt > /dev/null' % (entries_per_rxq, rxq)
        subprocess.check_call(cmd, shell=True)

def hier_config_xps(config):
    if config.qmodel == QMODEL_MQ:
        # Use the Intel script to configure XPS
        subprocess.call('sudo killall irqbalance', shell=True)
        xps_script = DRIVER_DIR + '/scripts/set_irq_affinity'
        subprocess.call('sudo %s -x all %s' % (xps_script, config.iface),
            shell=True)

        # Also configure RFS
        hier_configure_rfs(config)
    else:
        print 'Skipping XPS for qmodel: %s' % config.qmodel

        #Note: maybe not necessary.  But it shouldn't hurt to restart irqbalance
        subprocess.call('sudo service irqbalance restart', shell=True)

def hier_config_qdisc(config):
    #XXX: DEBUG:
    #print 'WARNING: Skipping Qdisc config!'
    #return
    if config.qmodel == QMODEL_MQPRI or config.qmodel == QMODEL_MQETS:
        print 'WARNING: Skipping Qdisc for qmodel \'%s\' because it does ' \
            'not work yet!' % config.qmodel
        return

    qcnt = len(get_txqs(config))
    for i in xrange(1, qcnt + 1):
        #
        # Configure a DRR Qdisc prio Qdisc per txq with Prio children.
        #

        # Configrue the DRR Qdiscs
        #  ... Create the classes for Tenant1 (%d00:1) and Tenant2 (%d00:2)
        tc_cmd = 'sudo tc qdisc add dev %s parent :%x handle %d00: drr' % \
            (config.iface, i, i)
        subprocess.check_call(tc_cmd, shell=True)
        tc_cmd = 'sudo tc class add dev %s parent %d00: classid %d00:1 drr quantum %d' % \
            (config.iface, i, i, config.drr_quantum)
        subprocess.check_call(tc_cmd, shell=True)
        tc_cmd = 'sudo tc class add dev %s parent %d00: classid %d00:2 drr quantum %d' % \
            (config.iface, i, i, config.drr_quantum)
        subprocess.check_call(tc_cmd, shell=True)

        # Note: this only works if the high priority ports are on Tenant1

        # Create traffic filters to send traffic from the second spark
        # instance (ubuntu2) to :2
        for p in H2_PORTS:
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

        # Configure the prio children
        for drr_class in ['%d00:1' % i, '%d00:2' % i]:
            handle = '%d0%s' % (i, (drr_class.split(':')[-1]))
            tc_cmd = 'sudo tc qdisc add dev %s parent %s handle %s: prio' % \
                (config.iface, drr_class, handle)
            subprocess.check_call(tc_cmd, shell=True)
            #tc_cmd = 'sudo tc qdisc add dev %s parent %s:1 sfq limit 32768 perturb 60' % \
            tc_cmd = 'sudo tc qdisc add dev %s parent %s:1 pfifo_fast' % \
                (config.iface, handle)
            subprocess.check_call(tc_cmd, shell=True)
            #tc_cmd = 'sudo tc qdisc add dev %s parent %s:2 sfq limit 32768 perturb 60' % \
            tc_cmd = 'sudo tc qdisc add dev %s parent %s:2 pfifo_fast' % \
                (config.iface, handle)
            subprocess.check_call(tc_cmd, shell=True)
            #tc_cmd = 'sudo tc qdisc add dev %s parent %s:3 sfq limit 32768 perturb 60' % \
            tc_cmd = 'sudo tc qdisc add dev %s parent %s:3 pfifo_fast' % \
                (config.iface, handle)
            subprocess.check_call(tc_cmd, shell=True)

            # Create a filter for memcached traffic
            for p in PRI_PORTS:
                for pdir in ['sport', 'dport']:
                    tc_str = 'sudo tc filter add dev %s protocol ip parent %s: ' + \
                        'prio 1 u32 match ip %s %d 0xffff flowid %s:1'
                    tc_cmd = tc_str % (config.iface, handle, pdir, p, handle)
                    subprocess.check_call(tc_cmd, shell=True)

            # Create a traffic filter to send the rest of the traffic to priority :2
            tc_str = 'sudo tc filter add dev %s protocol all parent %s: ' + \
                'prio 2 u32 match ip dst 0.0.0.0/0 flowid %s:2'
            tc_cmd = tc_str % (config.iface, handle, handle)
            subprocess.check_call(tc_cmd, shell=True)

def hier_config_server(config):
    # Configure the number of NIC queues
    hier_config_nic_driver(config)

    # Configure XPS
    hier_config_xps(config)

    # Configure cgroups
    #XXX: Currently located in hier_config_nic_driver
    #hier_config_cgroups(config)

    # Configure Qdisc/TC
    #XXX: BUG: There appears to be a bug with assigning Qdiscs in the mqprio
    # Qdisc.  Because the first |tc| classes of the mqprio qdisc are for the
    # traffic class, tc will not allow a new qdisc to be attached.  However,
    # from debugging, it seems like attaching to classes |tc| + 1 : |tc| +
    # |queues| + 1 leads to the wrong queues being used.
    if config.qmodel == QMODEL_MQPRI or config.qmodel == QMODEL_MQETS:
        print 'Skipping Qdisc config for qmodel: %s' % config.qmodel
    else:
        hier_config_qdisc(config)

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
        config = HierConfig(user_config)
    else:
        config = HierConfig()

    # Configure the server
    hier_config_server(config)

if __name__ == '__main__':
    main()
