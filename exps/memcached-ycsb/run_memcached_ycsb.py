#!/usr/bin/python

import argparse
import glob
import os
import platform
import re
import subprocess
import yaml
from time import sleep

YCSB_DIR = '/users/brents/YCSB/'
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
        '-P %(workload)s -P %(properties)s -threads 32 -p status.interval=1'
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
    bytes_per_op = (32 * 1024)
    ops_persec = -1
    ival_gbps = []
    for l in out.split('\n'):
        match = re.match(r".*OVERALL.*Throughput.*, (%s).*" \
            % FLOAT_DESC_STR, l)
        if match:
            ops_persec = float(match.groups()[0])

        # Interval gbps
        match = re.match(r".*sec:.*operations; (%s) current ops/sec.*" % FLOAT_DESC_STR, l)
        if match:
            opps = float(match.groups()[0])
            igbps = opps * bytes_per_op * 8 / 1e9
            ival_gbps.append(igbps)

    # Convert throughput to bps
    gbps = ops_persec * bytes_per_op * 8 / 1e9

    # Throw away the first second of intervals because its always slow and the
    # last second of intervals because its incomplete
    ival_gbps = ival_gbps[1:-1]

    results = {'gbps': gbps, 'ival_gbps': ival_gbps}
    return results

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
    results = memcached_ycsb_run()

    #DEBUG
    print yaml.dump(results)

    # Output the results
    if args.expname:
        res_fname = 'results/memcached_rate.%s.yaml' % args.expname
        with open(res_fname, 'w') as resf:
            yaml.dump(results, resf)

if __name__ == '__main__':
    main()
