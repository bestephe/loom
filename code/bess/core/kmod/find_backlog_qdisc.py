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

def check_backlog(iface):
    stats_cmd = 'sudo tc -s -d qdisc show dev %s' % iface
    qstats = subprocess.check_output(stats_cmd, shell=True)
    qstats = qstats.split('\n')

    for i in range(len(qstats)):
        line = qstats[i]
        #if re.match(r"qdisc.*", line):
        if re.match(r"qdisc.*drr", line):
            backlog_line = qstats[i+2]
            match = re.match(r".*backlog\s(\d+)b.*", backlog_line)
            if match:
                backlog = int(match.groups()[0])
                if backlog > 0:
                    print line
                    print backlog_line
                    print

def check_ethtool_stats(iface):
    stats_cmd = 'sudo ethtool -S %s' % iface
    ethstats = subprocess.check_output(stats_cmd, shell=True)
    ethstats = ethstats.split('\n')

    for i in range(len(ethstats)):
        line = ethstats[i]
        if re.match(r".*tx_queue_.*_stop_queue.*", line):
            match = re.match(r".*stop_queue.*(\d+).*", line)
            numstops = int(match.groups()[0])
            match = re.match(r".*restart_queue.*(\d+).*", ethstats[i + 1])
            numrestarts = int(match.groups()[0])
            if numstops != numrestarts:
                print line
                print ethstats[i + 1]
                print '\n'.join(ethstats[i - 8: i])
                print

def main():
    # Parse arguments
    parser = argparse.ArgumentParser(description='Find a qdisc that is backlogged')
    parser.add_argument('--iface', help='The interface to check.', required=True)
    args = parser.parse_args()

    # Check the qdiscs
    check_backlog(args.iface)

    # Check the ethtool stats
    check_ethtool_stats(args.iface)

if __name__ == '__main__':
    main()
