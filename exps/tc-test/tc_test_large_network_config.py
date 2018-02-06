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

sys.path.insert(0, os.path.abspath('..'))
from loom_exp_common import *

if 'LOOM_HOME' in os.environ:
    LOOM_HOME = os.environ['LOOM_HOME']
else:
    LOOM_HOME = '/proj/opennf-PG0/exp/loomtest2/datastore/bes/git/loom-code/'

NUM_TENANTS = 96
TENANT_TO_PORTS = {tenant: [] for tenant in range(NUM_TENANTS)}
for tenant in range(NUM_TENANTS):
    for i in range(2):
        TENANT_TO_PORTS[tenant].append(5000 + i + (2 * tenant))
PRI_PORTS = [11111]
print 'TENANT_TO_PORTS:', TENANT_TO_PORTS

DRIVER_DIR = LOOM_HOME + '/code/ixgbe-5.0.4/'
TCP_BYTE_LIMIT_DIR = '/proc/sys/net/ipv4/tcp_limit_output_bytes'
TCP_QUEUE_SYSTEM_DEFAULT = 262144

QMODEL_SQ = 'sq'
QMODEL_MQ = 'mq'
QMODEL_MQPRI = 'mq-pri'
QMODEL_MQETS = 'mq-ets'
QMODEL_BESS = 'bess'
QMODELS = [QMODEL_SQ, QMODEL_MQ, QMODEL_MQPRI, QMODEL_MQETS, QMODEL_BESS]

TCTEST_CONFIG_DEFAULTS = {
    'qmodel': QMODEL_SQ,

    'iface': 'eno2',
    'ifaces': ['loom1'],
    'iface_addr': '0000:81:00.1',
    'bessctl': 'bessctl/sq.bess',
    'bql_limit_max': (256 * 1024),
    'smallq_size': TCP_QUEUE_SYSTEM_DEFAULT,
    'qdisc': True,
    'cgroups': {'high_prio': 3},
    'xps': True,

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

class TcTestConfig(object):
    def __init__(self, *initial_data, **kwargs):
        for key in TCTEST_CONFIG_DEFAULTS:
            setattr(self, key, TCTEST_CONFIG_DEFAULTS[key])
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

# DCB helper function for MQ-PRI
def verify_dcb(config):
    dcb_check_cmd = 'sudo dcbtool gc %s dcb' % config.iface
    out = subprocess.check_output(dcb_check_cmd, shell=True)
    out_split = out.split('\n')
    off_match = re.match(r".*DCB State.*off.*", out_split[-2])
    on_match = re.match(r".*DCB State.*on.*", out_split[-2])
    assert(off_match != None or on_match != None)
    if config.qmodel == QMODEL_MQPRI:
        assert(on_match)
    else:
        assert(off_match)

#
# The rest of the functions
#
def tctest_config_nic_driver(config):
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

    # Assign additional IPs
    for ip_part in ['101', '102']:
        ip_split = ip.split('.')
        ip_split[2] = ip_part
        ip_extra = '.'.join(ip_split)
        ip_cmd = 'sudo ip addr add %s/24 dev %s' % (ip_extra, config.iface)
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
        # None of this seems to work
        ##dcb_cmd = 'sudo dcbtool sc %s pfc e:0 w:0 a:0 pfcup:00000000' % config.iface
        ##subprocess.check_call(dcb_cmd, shell=True)
        #dcb_cmd = 'sudo dcbtool sc %s pg e:1 w:0 a:0 strict:00111111' % config.iface
        ##dcb_cmd = 'sudo dcbtool sc %s pg strict:00010000' % config.iface
        #subprocess.check_call(dcb_cmd, shell=True)
        ##dcb_cmd = 'sudo dcbtool sc %s pg pgpct:0,0,0,100,0,0,0,0' % config.iface
        #dcb_cmd = 'sudo dcbtool sc %s pg pgpct:25,75,0,0,0,0,0,0' % config.iface
        #subprocess.check_call(dcb_cmd, shell=True)

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
    else:
        # Disable any DCB traffic classes/priorities in case they have been
        # previously enabled
        sleep(1.5) #XXX: dcbtool fails otherwise
        dcb_cmd = 'sudo dcbtool sc %s dcb off' % config.iface
        subprocess.check_call(dcb_cmd, shell=True)

    # Verify that DCB is configured properly
    sleep(1.5) #XXX: dcbtool fails otherwise
    verify_dcb(config)

    # Kill lldpad because for some reason it seems to reconfigure the NIC
    # into a DCB-off state automatically
    lldp_cmd = 'sudo service lldpad stop'
    subprocess.check_call(lldp_cmd, shell=True)

def tctest_config_xps(config):
    if config.qmodel == QMODEL_MQ:
        if config.xps:
            # Use the Intel script to configure XPS
            subprocess.call('sudo killall irqbalance', shell=True)
            xps_script = DRIVER_DIR + '/scripts/set_irq_affinity'
            subprocess.call('sudo %s -x all %s' % (xps_script, config.iface),
                shell=True)
        else:
            print 'Disabling XPS'
            txqs = get_txqs(config.iface)
            for txq in txqs:
                cmd = 'echo 0 | sudo tee /%s/xps_cpus' % txq
                subprocess.check_call(cmd, shell=True)

        # Also configure RFS
        configure_rfs(config, config.iface)
    else:
        print 'Skipping XPS for qmodel: %s' % config.qmodel

        #Note: maybe not necessary.  But it shouldn't hurt to restart irqbalance
        subprocess.call('sudo service irqbalance restart', shell=True)

def tctest_config_qdisc(config, iface):
    #XXX: DEBUG:
    #print 'WARNING: Skipping Qdisc config!'
    #return

    if config.qmodel == QMODEL_MQPRI or config.qmodel == QMODEL_MQETS:
        print 'WARNING: Skipping Qdisc for qmodel \'%s\' because it does ' \
            'not work yet!' % config.qmodel
        return

    qcnt = len(get_txqs(iface))
    for i in xrange(1, qcnt + 1):
        #
        # Configure a DRR Qdisc per txq with Prio children.
        #
        # Note: this does not fully install the hierarchy.  Importantly, it
        #   skips adding priority within a single tenant.

        # Configrue the DRR Qdiscs
        #  Create the classes for T0 (%d00:1), T1 (%d00:2), ...
        try:
            tc_cmd = 'sudo tc qdisc add dev %s parent :%x handle %d00: drr' % \
                (iface, i, i)
            subprocess.check_call(tc_cmd, shell=True)
        except subprocess.CalledProcessError:
            tc_cmd = 'sudo tc qdisc add dev %s root handle %d00: drr' % \
                (iface, i)
            subprocess.check_call(tc_cmd, shell=True)



        for tenant in range(NUM_TENANTS):
            thandle = tenant + 1
            tc_cmd = 'sudo tc class add dev %s parent %d00: classid %d00:%d drr quantum %d' % \
                (iface, i, i, thandle, config.drr_quantum)
            subprocess.check_call(tc_cmd, shell=True)

        # Create traffic filters for each tenant
        for tenant in range(NUM_TENANTS):
            thandle = tenant + 1
            ports = TENANT_TO_PORTS[tenant]
            for p in ports:
                for pdir in ['sport', 'dport']:
                    tc_str = 'sudo tc filter add dev %s protocol ip parent %d00: ' + \
                        'prio 1 u32 match ip %s %d 0xffff flowid %d00:%d'
                    tc_cmd = tc_str % (iface, i, pdir, p, i, thandle)
                    subprocess.check_call(tc_cmd, shell=True)

        # Create a traffic filter to send the rest of the traffic to class :1 (T0)
        tc_str = 'sudo tc filter add dev %s protocol all parent %d00: ' + \
            'prio 2 u32 match ip dst 0.0.0.0/0 flowid %d00:1'
        tc_cmd = tc_str % (iface, i, i)
        subprocess.check_call(tc_cmd, shell=True)

        # Configure the prio children
        drr_classes = ['%d00:%d' % (i, thandle) for thandle in range(1, NUM_TENANTS + 1)]
        for drr_class in drr_classes:
            sub_i = int(drr_class.split(':')[-1])
            handle = '%d%02d' % (i, sub_i)
            print 'drr_class:', drr_class, 'handle:', handle
            tc_cmd = 'sudo tc qdisc add dev %s parent %s handle %s: prio' % \
                (iface, drr_class, handle)
            subprocess.check_call(tc_cmd, shell=True)
            #tc_cmd = 'sudo tc qdisc add dev %s parent %s:1 sfq limit 32768 perturb 60' % \
            tc_cmd = 'sudo tc qdisc add dev %s parent %s:1 pfifo_fast' % \
                (iface, handle)
            subprocess.check_call(tc_cmd, shell=True)
            #tc_cmd = 'sudo tc qdisc add dev %s parent %s:2 pfifo_fast' % \
            tc_cmd = 'sudo tc qdisc add dev %s parent %s:2 sfq limit 32768 perturb 60' % \
                (iface, handle)
            subprocess.check_call(tc_cmd, shell=True)
            #tc_cmd = 'sudo tc qdisc add dev %s parent %s:3 pfifo_fast' % \
            tc_cmd = 'sudo tc qdisc add dev %s parent %s:3 sfq limit 32768 perturb 60' % \
                (iface, handle)
            subprocess.check_call(tc_cmd, shell=True)

            # Create a filter for high priority traffic
            for p in PRI_PORTS:
                for pdir in ['sport', 'dport']:
                    tc_str = 'sudo tc filter add dev %s protocol ip parent %s: ' + \
                        'prio 1 u32 match ip %s %d 0xffff flowid %s:1'
                    tc_cmd = tc_str % (iface, handle, pdir, p, handle)
                    subprocess.check_call(tc_cmd, shell=True)

            # Create a traffic filter to send the rest of the traffic to priority :2
            tc_str = 'sudo tc filter add dev %s protocol all parent %s: ' + \
                'prio 2 u32 match ip dst 0.0.0.0/0 flowid %s:2'
            tc_cmd = tc_str % (iface, handle, handle)
            subprocess.check_call(tc_cmd, shell=True)

#def tctest_config_cgrules(config):
#    for user, net_cgroup in [('ubuntu', 'tc1'), ('ubuntu2', 'tc3')]:
#        # Config cgrules so that all spark traffic from ubuntu uses high_prio
#        cgrule_cmd = 'echo \'%s net_prio %s\' | sudo tee -a /etc/cgrules.conf' % \
#            (user, net_cgroup)
#        subprocess.check_call(cgrule_cmd, shell=True)
#
#        # Kill and restart cgrulesengd
#        cg_cmd = 'sudo killall cgrulesengd'
#        subprocess.call(cg_cmd, shell=True)
#        cg_cmd = 'echo "" | sudo tee /etc/cgconfig.conf'
#        subprocess.check_call(cg_cmd, shell=True)
#        cg_cmd = 'sudo cgrulesengd'
#        subprocess.check_call(cg_cmd, shell=True)
#
#        # Use cgclassify to configure the priority of all programs for ubuntu (Spark)
#        #XXX: NOTE: may not be necessary?
#        get_pids_cmd = 'ps aux | grep "^%s " | awk \'{ print $2 }\'' % user
#        ubuntu_pids = subprocess.check_output(get_pids_cmd, shell=True)
#        ubuntu_pids = ' '.join(ubuntu_pids.split())
#        cg_cmd = 'sudo cgclassify -g net_prio:%s --cancel-sticky %s' % \
#            (net_cgroup, ubuntu_pids)
#        subprocess.check_call(cg_cmd, shell=True)

def tctest_config_server(config):
    # Kill BESS
    subprocess.call('sudo killall bessd', shell=True)

    # Configure the number of NIC queues
    tctest_config_nic_driver(config)

    # Configure XPS
    tctest_config_xps(config)

    # Configure Qdisc/TC
    #XXX: BUG: There appears to be a bug with assigning Qdiscs in the mqprio
    # Qdisc.  Because the first |tc| classes of the mqprio qdisc are for the
    # traffic class, tc will not allow a new qdisc to be attached.  However,
    # from debugging, it seems like attaching to classes |tc| + 1 : |tc| +
    # |queues| + 1 leads to the wrong queues being used.
    if config.qmodel == QMODEL_MQPRI or (not config.qdisc):
        print 'Skipping Qdisc config for qmodel: %s' % config.qmodel
    else:
            tctest_config_qdisc(config, config.iface)

    # Configure CGroups
    config_cgroup(config, config.iface)

    # Configure CGroup rules
    #XXX: Not needed as this experiment uses cgexec
    #tctest_config_cgrules(config)

    # Configure BQL
    set_all_bql_limit_max(config, config.iface)

def tctest_config_bess(config):
    subprocess.call('sudo killall tcpdump', shell=True)

    loom_config_bess(config)

    # Do not configure XPS (for now)
    subprocess.call('sudo service irqbalance restart', shell=True)

    # Configure all of the interfaces
    #XXX: This code doesn't work for virtio/Vhost/TAP interfaces
    for iface in config.ifaces:
        # Configure Qdisc/TC
        #TODO: optionally skip Qdisc config
        if config.qdisc:
            tctest_config_qdisc(config, iface)

        # Configure CGroups
        config_cgroup(config, iface)

        # Configure CGroup rules
        #XXX: Not needed as this experiment uses cgexec
        #tctest_config_cgrules(config)

        # Configure RFS
        configure_rfs(config, iface)
    
        # Configure BQL
        set_all_bql_limit_max(config, iface)

def main():
    # Parse arguments
    parser = argparse.ArgumentParser(description='Configure the server'
        'for the tc-test experiment')
    parser.add_argument('--config', help='An alternate configuration file. '
        'The configuration format is unsurprisingly not documented.')
    args = parser.parse_args()

    # Get the config
    if args.config:
        with open(args.config) as configf:
            user_config = yaml.load(configf)
        config = TcTestConfig(user_config)
    else:
        config = TcTestConfig()

    print 'config:', config.dump()

    # Configure the server
    if config.qmodel == QMODEL_BESS:
        tctest_config_bess(config)
    else:
        tctest_config_server(config)

if __name__ == '__main__':
    main()
