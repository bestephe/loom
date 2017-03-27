#!/usr/bin/python

import argparse
import glob
import os
import platform
import re
import subprocess
import yaml
from time import sleep

#YCSB_DIR = '/home/ubuntu/YCSB/'
YCSB_DIR = '/users/brents/YCSB/'
CUR_DIR = os.getcwd()

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

def memcached_ycsb_run(args):
    #XXX: There may be multiple ways to get latency for different intervals.
    # If I have troubles, this approach may be reasonable.  For more help, see:
    # https://github.com/brianfrankcooper/YCSB/blob/master/core/CHANGES.md
    #memcached_histf = '%s/results/memcached_histf.%s.' % (CUR_DIR, args.expname)
    #run_cmd_tmpl = 'cd %(ycsb_dir)s; ./bin/ycsb run memcached -s ' \
    #    '-P %(workload)s -P %(properties)s -threads 16 -p hdrhistogram.fileoutput=true ' \
    #    '-p hdrhistogram.output.path=%(histf)s'
    #run_cmd_args = {
    #    'ycsb_dir': YCSB_DIR, 
    #    'workload': os.path.abspath(WORKLOAD),
    #    'properties': os.path.abspath(PROPERTIES),
    #    'histf': memcached_histf,
    #}

    run_cmd_tmpl = 'cd %(ycsb_dir)s; %(prefix)s ./bin/ycsb run memcached -s ' \
        '-P %(workload)s -P %(properties)s -threads 16 -p status.interval=1'
    if args.high_prio:
        prefix = 'sudo cgexec -g net_prio:high_prio '
    else:
        prefix = ''
    run_cmd_args = {
        'ycsb_dir': YCSB_DIR, 
        'workload': os.path.abspath(WORKLOAD),
        'properties': os.path.abspath(PROPERTIES),
        'prefix': prefix,
    }
    run_cmd = run_cmd_tmpl % run_cmd_args

    print 'run_cmd:', run_cmd
    proc = subprocess.Popen(run_cmd, shell=True,
        stderr=subprocess.STDOUT, stdout=subprocess.PIPE)
    out = proc.stdout.read()
    print 'Run output:'
    print out

    # Get the throughput
    ops_persec = -1
    overall_latencies = {}
    ival_latencies = {}
    for l in out.split('\n'):
        # Tput
        match = re.match(r".*OVERALL.*Throughput.*, (%s).*" \
            % FLOAT_DESC_STR, l)
        if match:
            ops_persec = float(match.groups()[0])

        # Overall Latencies
        match = re.match(r".*READ.*, (\d+)thPercentileLatency.*, (%s).*" \
            % (FLOAT_DESC_STR), l)
        if match:
            ptile = match.groups()[0] + 'p'
            latency = float(match.groups()[1])
            overall_latencies[ptile] = latency
        match = re.match(r".*READ.*, MaxLatency.*, (%s).*" \
            % (FLOAT_DESC_STR), l)
        if match:
            latency = float(match.groups()[0])
            overall_latencies['max'] = latency

        # Interval Latencies
        match = re.match(r".* (\d+) sec:.*READ:.*Avg=(%s), 90=(%s), 99=(%s).*" % \
            (FLOAT_DESC_STR, FLOAT_DESC_STR, FLOAT_DESC_STR), l)
        cleanup_match = re.match(r".*CLEANUP.*", l)
        if match and not cleanup_match:
            groups = match.groups()
            secs = int(groups[0])
            avg = float(groups[1])
            p90 = float(groups[5])
            p99 = float(groups[9])
            ival_latencies[secs] = {'avg': avg, '90p': p90, '99p': p99}

            #XXX: DEBUG
            #print 'ival:', l
            #print match.groups()
            #print

    # Convert throughput to bps
    bytes_per_op = (32 * 1024)
    gbps = ops_persec * bytes_per_op * 8 / 1e9
    print 'Gbps:', gbps

    return ival_latencies

def main():
    # Parse arguments
    parser = argparse.ArgumentParser(description='Run YCSB for memcached for '
        'the rate-limiting and fairness experiment')
    parser.add_argument('--expname', help='A string to use to identify the '
        'output file from this experiment.')
    #XXX: This could be in the config.  But currently the config isn't used.
    parser.add_argument('--high-prio', help='Start the memcached process '
        'at a high network priority. Overrides the config', action='store_true')
    args = parser.parse_args()

    # Connect to the remote host, configure it, and start the memcached servers
    #TODO: This can be done with paramiko and the start_memcached.py script.
    # However, I'm leaving this until later for now.

    # YCSB: Load the database
    memcached_ycsb_load()

    # YCSB: Run the benchmark
    latencies = memcached_ycsb_run(args)

    # Output the results
    print 'latencies:', latencies
    if args.expname:
        res_fname = 'results/memcached_latency.%s.yaml' % args.expname
        with open(res_fname, 'w') as resf:
            yaml.dump({'latencies': latencies}, resf)

if __name__ == '__main__':
    main()
