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

def gen_perf_debug():
    for num_iperf_tenants in [1, 4, 8, 16, 32]:
        for num_conns in [1, 4, 16]:
            apps = []

            for tenant in range(num_iperf_tenants):
                tc = (tenant << 1) + 1
                tc_str = 'tc%d' % tc
                base_port = IPERF_BASEPORT + (2 * tenant)
                base_name = 'iperf_%s_' % tc_str
                num_apps = 2
                start = 0
                finish = 8

                for i in range(num_apps):
                    assert(i < 2)
                    iperf = {'cgroup': tc_str, 'ip': SINK_IP0, 'port': base_port + i,
                        'prog': 'iperf3', 'name': '%s%d' % (base_name, i), 'num_conns': num_conns,
                        'start': start, 'finish': finish}
                    apps.append(iperf)

            expname = 'perf_debug_%dten_%dconn' % (num_iperf_tenants, num_conns)
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
    parser = argparse.ArgumentParser(description='Generate configs for debugging 40G performance.')
    args = parser.parse_args()

    gen_perf_debug()

if __name__ == '__main__':
    main()
