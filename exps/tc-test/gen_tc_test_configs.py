#!/usr/bin/python

import argparse
import glob
import os
import platform
import re
import subprocess
import sys
import yaml

from tc_test_common import *

SINK_IP0 = '10.10.101.2' # No rate-limit
SINK_IP1 = '10.10.102.2' # Expected to be rate-limited to 2.5Gbps
SRC_IP = '10.10.101.1'

SOCKPERF_BASEPORT = 11111
IPERF_BASEPORT = 5000

def gen_tctest_config1():
    num_iperf_tenants = 4
    apps = []

    # Start measuring latency at time 0 at tc-0
    sockperf_finish = (4 * num_iperf_tenants) + 2
    sockperf_tc0 = {'cgroup': 'tc0', 'ip': SINK_IP0, 'port': SOCKPERF_BASEPORT,
        'prog': 'sockperf', 'name': 'sockperf_0', 'start': 0,
        'finish': sockperf_finish}
    apps.append(sockperf_tc0)

    for tenant in range(1, num_iperf_tenants+1):
        tc = (tenant << 1) + 1
        tc_str = 'tc%d' % tc
        base_port = IPERF_BASEPORT + (100 * tenant)
        base_name = 'iperf_%s_' % tc_str
        num_conns = (4 ** tenant)
        start = 2 * tenant
        finish = (4 * num_iperf_tenants) - ((tenant - 1) * 2)

        if tenant == 1:
            iperf0 = {'cgroup': tc_str, 'ip': SINK_IP0, 'port': base_port,
                'prog': 'iperf3', 'name': base_name + '0', 'num_conns': (num_conns+1)/2,
                'start': start, 'finish': finish}
            iperf1 = {'cgroup': tc_str, 'ip': SINK_IP0, 'port': base_port + 1,
                'prog': 'iperf3', 'name': base_name + '1', 'num_conns': num_conns/2,
                'start': start, 'finish': finish}
            apps.append(iperf0)
            apps.append(iperf1)
        else:
            for i in range(num_conns / 4):
                assert(i < 100)
                iperf = {'cgroup': tc_str, 'ip': SINK_IP0, 'port': base_port + i,
                    'prog': 'iperf3', 'name': '%s%d' % (base_name, i), 'num_conns': 4,
                    'start': start, 'finish': finish}
                apps.append(iperf)

    c1 = {
        'apps': apps,
        'expname': 'tctest_conf1',
        'sink_host': SINK_IP0,
        'src_host': SRC_IP,
    }

    with open('configs/tctest_conf1.yaml', 'w') as outf:
        yaml.dump(c1, outf, default_flow_style=False)

def gen_tctest_config_rl1():
    num_iperf_tenants = 4
    apps = []

    # Start measuring latency at time 0 at tc-0
    sockperf_finish = (4 * num_iperf_tenants) + 2
    sockperf_tc0 = {'cgroup': 'tc0', 'ip': SINK_IP0, 'port': SOCKPERF_BASEPORT,
        'prog': 'sockperf', 'name': 'sockperf_0', 'start': 0,
        'finish': sockperf_finish}
    apps.append(sockperf_tc0)

    for tenant in range(1, num_iperf_tenants+1):
        tc = (tenant << 1) + 1
        tc_str = 'tc%d' % tc
        base_port = IPERF_BASEPORT + (100 * tenant)
        base_name = 'iperf_%s_' % tc_str
        num_conns = (4 ** tenant)
        start = 2 * tenant
        finish = (4 * num_iperf_tenants) - ((tenant - 1) * 2)

        if tenant == 1:
            dst0_iperf0 = {'cgroup': tc_str, 'ip': SINK_IP0, 'port': base_port,
                'prog': 'iperf3', 'name': base_name + 'dst0_0', 'num_conns': 2,
                'start': start, 'finish': finish}
            dst1_iperf0 = {'cgroup': tc_str, 'ip': SINK_IP1, 'port': base_port + 1,
                'prog': 'iperf3', 'name': base_name + 'dst1_0', 'num_conns': 2,
                'start': start, 'finish': finish}
            dst1_iperf1 = {'cgroup': tc_str, 'ip': SINK_IP1, 'port': base_port + 2,
                'prog': 'iperf3', 'name': base_name + 'dst1_1', 'num_conns': 2,
                'start': start, 'finish': finish}
            apps.append(dst0_iperf0)
            apps.append(dst1_iperf0)
            apps.append(dst1_iperf1)

        else:
            for i in range(num_conns / 4):
                assert(i < 100)
                iperf = {'cgroup': tc_str, 'ip': SINK_IP0, 'port': base_port + i,
                    'prog': 'iperf3', 'name': '%s%d' % (base_name, i), 'num_conns': 4,
                    'start': start, 'finish': finish}
                apps.append(iperf)

    c1 = {
        'apps': apps,
        'expname': 'tctest_rl_conf1',
        'sink_host': SINK_IP0,
        'src_host': SRC_IP,
    }

    with open('configs/tctest_rl_conf1.yaml', 'w') as outf:
        yaml.dump(c1, outf, default_flow_style=False)

def main():
    parser = argparse.ArgumentParser(description='Generate configs for the tc-test experiment.')
    args = parser.parse_args()

    gen_tctest_config1()
    gen_tctest_config_rl1()

if __name__ == '__main__':
    main()
