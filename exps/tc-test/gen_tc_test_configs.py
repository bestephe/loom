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

#SOCKPERF_BASEPORT = 11111
SOCKPERF_BASEPORT = 11000
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
        num_conns = (4 ** (tenant))
        start = 2 * tenant
        finish = (4 * num_iperf_tenants) - ((tenant - 1) * 2)

        if tenant == 1:
            num_conns *= 4
            for i in range(num_conns / 4):
                assert(i < 100)
                iperf = {'cgroup': tc_str, 'ip': SINK_IP0, 'port': base_port + i,
                    'prog': 'iperf3', 'name': '%s%d' % (base_name, i), 'num_conns': 4,
                    'start': start, 'finish': finish}
                apps.append(iperf)
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
    #sockperf_finish = (4 * num_iperf_tenants) + 2
    #sockperf_tc0 = {'cgroup': 'tc0', 'ip': SINK_IP0, 'port': SOCKPERF_BASEPORT,
    #    'prog': 'sockperf', 'name': 'sockperf_0', 'start': 0,
    #    'finish': sockperf_finish}
    #apps.append(sockperf_tc0)

    for tenant in range(1, num_iperf_tenants+1):
        tc = (tenant << 1) + 1
        tc_str = 'tc%d' % tc
        base_port = IPERF_BASEPORT + (100 * tenant)
        base_name = 'iperf_%s_' % tc_str
        num_conns = (4 ** tenant)
        if tenant == num_iperf_tenants:
            num_conns = (4 ** (tenant - 1))

        start = 2 * tenant
        finish = (2 * num_iperf_tenants) + 2
        #finish = (4 * num_iperf_tenants) - ((tenant - 1) * 2)

        #start = 0 if tenant == 1 else 5
        ##finish = 10

        if tenant == 1:
            dst0_iperf0 = {'cgroup': tc_str, 'ip': SINK_IP0, 'port': base_port,
                'prog': 'iperf3', 'name': base_name + 'dst0_0', 'num_conns': 2,
                'start': start, 'finish': finish}
            apps.append(dst0_iperf0)
            dst1_iperf0 = {'cgroup': tc_str, 'ip': SINK_IP1, 'port': base_port + 1,
                'prog': 'iperf3', 'name': base_name + 'dst1_0', 'num_conns': 8,
                'start': start, 'finish': finish}
            apps.append(dst1_iperf0)
            dst1_iperf1 = {'cgroup': tc_str, 'ip': SINK_IP1, 'port': base_port + 2,
                'prog': 'iperf3', 'name': base_name + 'dst1_1', 'num_conns': 8,
                'start': start, 'finish': finish}
            apps.append(dst1_iperf1)
            dst0_iperf1 = {'cgroup': tc_str, 'ip': SINK_IP0, 'port': base_port + 3,
                'prog': 'iperf3', 'name': base_name + 'dst0_1', 'num_conns': 2,
                'start': start, 'finish': finish}
            apps.append(dst0_iperf1)
            dst0_iperf2 = {'cgroup': tc_str, 'ip': SINK_IP0, 'port': base_port + 4,
                'prog': 'iperf3', 'name': base_name + 'dst0_2', 'num_conns': 2,
                'start': start, 'finish': finish}
            apps.append(dst0_iperf2)
            dst0_iperf3 = {'cgroup': tc_str, 'ip': SINK_IP0, 'port': base_port + 5,
                'prog': 'iperf3', 'name': base_name + 'dst0_3', 'num_conns': 2,
                'start': start, 'finish': finish}
            apps.append(dst0_iperf3)
            dst0_iperf4 = {'cgroup': tc_str, 'ip': SINK_IP0, 'port': base_port + 6,
                'prog': 'iperf3', 'name': base_name + 'dst0_4', 'num_conns': 2,
                'start': start, 'finish': finish}
            apps.append(dst0_iperf4)
            dst0_iperf5 = {'cgroup': tc_str, 'ip': SINK_IP0, 'port': base_port + 7,
                'prog': 'iperf3', 'name': base_name + 'dst0_5', 'num_conns': 2,
                'start': start, 'finish': finish}
            apps.append(dst0_iperf5)
            dst0_iperf6 = {'cgroup': tc_str, 'ip': SINK_IP0, 'port': base_port + 8,
                'prog': 'iperf3', 'name': base_name + 'dst0_6', 'num_conns': 2,
                'start': start, 'finish': finish}
            apps.append(dst0_iperf6)
            dst0_iperf7 = {'cgroup': tc_str, 'ip': SINK_IP0, 'port': base_port + 9,
                'prog': 'iperf3', 'name': base_name + 'dst0_7', 'num_conns': 2,
                'start': start, 'finish': finish}
            apps.append(dst0_iperf7)
            dst1_iperf2 = {'cgroup': tc_str, 'ip': SINK_IP1, 'port': base_port + 10,
                'prog': 'iperf3', 'name': base_name + 'dst1_2', 'num_conns': 8,
                'start': start, 'finish': finish}
            apps.append(dst1_iperf2)
            dst1_iperf3 = {'cgroup': tc_str, 'ip': SINK_IP1, 'port': base_port + 11,
                'prog': 'iperf3', 'name': base_name + 'dst1_3', 'num_conns': 8,
                'start': start, 'finish': finish}
            apps.append(dst1_iperf3)

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

def gen_tctest_large():
    for num_iperf_tenants in [32, 64, 96]:
        for num_conns in [1, 4, 16]:
            apps = []

            for tenant in range(num_iperf_tenants):
                tc = (tenant << 1) + 1
                tc_str = 'tc%d' % tc
                base_port = IPERF_BASEPORT + (2 * tenant)
                base_name = 'iperf_%s_' % tc_str
                num_apps = 2
                start = 0
                finish = 20

                for i in range(num_apps):
                    assert(i < 2)
                    iperf = {'cgroup': tc_str, 'ip': SINK_IP0, 'port': base_port + i,
                        'prog': 'iperf3', 'name': '%s%d' % (base_name, i), 'num_conns': num_conns,
                        'start': start, 'finish': finish}
                    apps.append(iperf)

            expname = 'tctest_large_%dten_%dconn' % (num_iperf_tenants, num_conns)
            c1 = {
                'apps': apps,
                'expname': expname,
                'sink_host': SINK_IP0,
                'src_host': SRC_IP,
                'ethtool': True,
            }

            with open('configs/%s.yaml' % expname, 'w') as outf:
                yaml.dump(c1, outf, default_flow_style=False)


def gen_tctest_large_sockperf():
    for num_iperf_tenants in [32, 128]:
        for num_conns in [16, 256]:
            apps = []

            for tenant in range(num_iperf_tenants):
                tc = (tenant << 1) + 1
                tc_str = 'tc%d' % tc
                base_port = SOCKPERF_BASEPORT + (300 * tenant)
                base_name = 'sockperf_%s_' % tc_str
                num_apps = num_conns
                start = 0
                finish = 20

                for i in range(num_apps):
                    sockperf = {'cgroup': tc_str, 'ip': SINK_IP0, 'port': base_port + i,
                        'prog': 'sockperf', 'name': '%s%d' % (base_name, i), 'num_conns': num_conns,
                        'start': start, 'finish': finish, 'mps': 'max'}
                    apps.append(sockperf)

            expname = 'tctest_large_sockperf_%dten_%dconn' % (num_iperf_tenants, num_conns)
            c1 = {
                'apps': apps,
                'expname': expname,
                'sink_host': SINK_IP0,
                'src_host': SRC_IP,
                'ethtool': True,
            }

            with open('configs/%s.yaml' % expname, 'w') as outf:
                yaml.dump(c1, outf, default_flow_style=False)

def main():
    parser = argparse.ArgumentParser(description='Generate configs for the tc-test experiment.')
    args = parser.parse_args()

    gen_tctest_config1()
    gen_tctest_config_rl1()
    gen_tctest_large()
    gen_tctest_large_sockperf()

if __name__ == '__main__':
    main()
