#!/usr/bin/python

import argparse
import glob
import os
import platform
import re
import subprocess
import yaml
from time import sleep

YCSB_DIR = '/scratch/bes/git/YCSB/'
WORKLOAD = 'workload_read_asym'
PROPERTIES = 'memcached_read_asym.properties'

FLOAT_DESC_STR = '[-+]?(\d+(\.\d*)?|\.\d+)([eE][-+]?\d+)?'

def memcached_ycsb_load():
    load_cmd_tmpl = 'cd %(ycsb_dir)s; ./bin/ycsb load memcached -s ' \
        '-P %(workload)s -P %(properties)s'
    load_cmd_args = {
        'ycsb_dir': YCSB_DIR, 
        'workload': os.path.abspath(WORKLOAD),
        'properties': os.path.abspath(PROPERTIES),
    }
    load_cmd = load_cmd_tmpl % load_cmd_args
    subprocess.check_call(load_cmd, shell=True)

def memcached_ycsb_run():
    run_cmd_tmpl = 'cd %(ycsb_dir)s; ./bin/ycsb run memcached -s ' \
        '-P %(workload)s -P %(properties)s -threads 16'
    run_cmd_args = {
        'ycsb_dir': YCSB_DIR, 
        'workload': os.path.abspath(WORKLOAD),
        'properties': os.path.abspath(PROPERTIES),
    }
    run_cmd = run_cmd_tmpl % run_cmd_args
    proc = subprocess.Popen(run_cmd, shell=True,
        stderr=subprocess.STDOUT, stdout=subprocess.PIPE)
    out = proc.stdout.read()
    print 'Run output:'
    print out

    # Get the throughput
    ops_persec = -1
    for l in out.split('\n'):
        match = re.match(r".*OVERALL.*Throughput.*, (%s).*" \
            % FLOAT_DESC_STR, l)
        if match:
            ops_persec = float(match.groups()[0])

    # Convert throughput to bps
    bytes_per_op = (128 * 1024)
    gbps = ops_persec * bytes_per_op * 8 / 1e9
    return gbps

def main():
    # Parse arguments
    parser = argparse.ArgumentParser(description='Run YCSB for memcached for '
        'the rate-limiting and fairness experiment')
    parser.add_argument('--expname', help='A string to use to identify the '
        'output file from this experiment.')
    args = parser.parse_args()

    # Connect to the remote host, configure it, and start the memcached servers
    #TODO: This can be done with paramiko and the start_memcached.py script.
    # However, I'm leaving this until later for now.

    # YCSB: Load the database
    memcached_ycsb_load()

    # YCSB: Run the benchmark
    gbps = memcached_ycsb_run()

    # Output the results
    print 'gbps:', gbps
    if args.expname:
        res_fname = 'results/memcachd_rate.%s.yaml' % args.expname
        with open(res_fname, 'w') as resf:
            yaml.dump({'gbps': gbps}, resf)

if __name__ == '__main__':
    main()
