#!/usr/bin/python

import argparse
import glob
import json
import numpy
import os
import paramiko
import platform
import re
import scipy
import scipy.stats
import scipy.interpolate
import subprocess
import sys
import yaml
from time import sleep

USERNAME = 'brents'

if 'LOOM_HOME' in os.environ:
    LOOM_HOME = os.environ['LOOM_HOME']
else:
    LOOM_HOME = '/proj/opennf-PG0/exp/loomtest2/datastore/bes/git/loom-code/'

TEST_PROG_IPERF = 'iperf3'
TEST_PROG_SOCKPERF = 'sockperf'
TEST_PROGS = [TEST_PROG_IPERF, TEST_PROG_SOCKPERF]

TESTPROG_CONFIG_DEFAULTS = {
    'prog': TEST_PROG_IPERF,
    'name': 'iperf_1',
    'ip': '10.10.101.2',
    'port': 5001,
    'start': 0,
    'finish': 10,
    'num_conns': 1,
    'cgroup': None,
}

TCTEST_CONFIG_DEFAULTS = {
    'apps': [TESTPROG_CONFIG_DEFAULTS],
    'src_host': '10.10.101.1',
    'sink_host': '10.10.101.2',
    'run': 1,
    'expname': 'test',
    'extra_name': '',
}

class TestProgConfig(object):
    def __init__(self, *initial_data, **kwargs):
        for key in TESTPROG_CONFIG_DEFAULTS:
            setattr(self, key, TESTPROG_CONFIG_DEFAULTS[key])
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
        if self.prog not in TEST_PROGS:
            raise ValueError('Unknown prog: \'%s\'' % self.prog)
    def dump(self):
        d = self.__dict__.copy()
        return d

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

        # Go from dictionary config to TestProgConfig
        self.apps = [TestProgConfig(app) for app in self.apps]
    def dump(self):
        d = self.__dict__.copy()
        d['apps'] = [app.dump() for app in d['apps']]
        return d
    def get_exp_str(self):
        if hasattr(self, 'extra_name') and self.extra_name != '':
            exp_str = 'results/tctest.%s.%s.%d.yaml' % \
                (self.expname, self.extra_name, self.run)
        else:
            exp_str = 'results/tctest.%s.%d.yaml' % (self.expname, self.run)
        return exp_str

#
# Per-app classes for convenience functions
#
class GenericOutput(object):
    pass

class GenericProg(object):
    def __init__(self, pconf):
        self.pconf = pconf

    def duration(self):
        return self.pconf.finish - self.pconf.start

    def get_cgroup_cmd(self, cmd):
        cgroup = self.pconf.cgroup
        if cgroup != None:
            cg_cmd = 'sudo cgexec -g net_prio:%(cgroup)s %(cmd)s'
        else:
            cg_cmd = 'sudo %(cmd)s'
        cg_cmd = cg_cmd % {'cgroup': cgroup, 'cmd': cmd}
        return cg_cmd

    def start_sink(self):
        sink_cmd = self.get_sink_cmd()
        cg_cmd = self.get_cgroup_cmd(sink_cmd)
        print 'start sink cg_cmd:', cg_cmd
        proc = subprocess.Popen(cg_cmd, shell=True,
            stderr=subprocess.STDOUT, stdout=subprocess.PIPE)
        self.dir = 'sink'
        self.proc = proc

    def start_src(self):
        src_cmd = self.get_src_cmd()
        cg_cmd = self.get_cgroup_cmd(src_cmd)
        #TODO: Deal with different desired start times
        print 'start src cg_cmd:', cg_cmd
        proc = subprocess.Popen(cg_cmd, shell=True,
            stderr=subprocess.STDOUT, stdout=subprocess.PIPE)
        self.dir = 'src'
        self.proc = proc

class IperfProg(GenericProg):
    def get_sink_cmd(self):
        cmd = 'iperf3 -s -p %(port)s'
        cmd = cmd % {'port': self.pconf.port}
        return cmd

    def get_src_cmd(self):
        pconf = self.pconf
        cmd = 'iperf3 -c %(ip)s -t %(duration)d -p %(port)s -J -Z -l 1M ' \
            '-P %(conns)d'
        cmd = cmd % {
            'ip': pconf.ip,
            'duration': self.duration(),
            'port': pconf.port,
            'conns': pconf.num_conns,
        }
        return cmd

    def process_agg_tput(self, json_obj):
        intervals = {}
        for i, interval in enumerate(json_obj["intervals"]):
            if i not in intervals:
                intervals[i] = []
            intervals_gbps = float(interval["sum"]["bits_per_second"])/1000000000
            intervals[i].append(intervals_gbps)
        agg_tputs = []
        for ival in intervals.values():
            agg_tputs.append(sum(ival))
        
        ivals = {self.pconf.start + i: agg_tput for i, agg_tput in enumerate(agg_tputs)}
        agg_tput_results = {'50p': get_percentile(agg_tputs, 50),
                            'avg': 1.0 * sum(agg_tputs) / len(agg_tputs),
                            'ivals': ivals
                            }
        return agg_tput_results

    def parse_src_output(self):
        assert(self.dir == 'src')
        proc_out = self.proc.stdout.read()
        #print 'proc_out:', proc_out
        json_obj = json.loads(proc_out)
        agg_tput_results = self.process_agg_tput(json_obj)
        #flow_tput_results = self.process_flow_tput(json_obj)
        #return {'agg_tput': agg_tput_results, 'flow_tput': flow_tput_results}
        return {'agg_tput': agg_tput_results}

    @staticmethod
    def killall():
        cmd = 'sudo killall iperf3'
        subprocess.call(cmd, shell=True)


#
# Generic Per-app setup
# 
#TEST_PROGS = [TEST_PROG_IPERF, TEST_PROG_SOCKPERF]
TEST_PROGS = [TEST_PROG_IPERF]
TEST_PROGS2CLASS = {
    TEST_PROG_IPERF: IperfProg,
    #TEST_PROG_SOCKPERF: 
}

def tc_test_start_sink(prog_conf):
    cls = TEST_PROGS2CLASS[prog_conf.prog]
    prog = cls(prog_conf)
    prog.start_sink()
    return prog

def tc_test_start_src(prog_conf):
    cls = TEST_PROGS2CLASS[prog_conf.prog]
    prog = cls(prog_conf)
    prog.start_src()
    return prog

def tc_test_killall():
    for test_prog in TEST_PROGS:
        cls = TEST_PROGS2CLASS[test_prog]
        cls.killall()

#
# Helpers
#
def connect_rhost(rhost):
    rssh = paramiko.SSHClient()
    rssh.load_system_host_keys()
    rssh.set_missing_host_key_policy(paramiko.AutoAddPolicy())

    ssh_config = paramiko.SSHConfig()
    user_config_file = os.path.expanduser("~/.ssh/config")
    if os.path.exists(user_config_file):
        with open(user_config_file) as f:
            ssh_config.parse(f)

    #cfg = {'hostname': rhost, 'username': options["username"]}
    cfg = {'hostname': rhost}

    user_config = ssh_config.lookup(cfg['hostname'])
    #for k in ('hostname', 'username', 'port'):
    for k in ('hostname', 'port'):
        if k in user_config:
            cfg[k] = user_config[k]
    cfg['username'] = USERNAME

    if 'proxycommand' in user_config:
        cfg['sock'] = paramiko.ProxyCommand(user_config['proxycommand'])

    rssh.connect(**cfg)

    return rssh

def get_percentile(data, percentile):
    return numpy.asscalar(scipy.stats.scoreatpercentile(data, percentile))

