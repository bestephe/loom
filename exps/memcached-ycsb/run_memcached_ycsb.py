#!/usr/bin/python

import argparse
import glob
import os
import platform
import subprocess
import yaml
from time import sleep

YCSB_DIR = '/scratch/bes/git/YCSB/'
WORKLOAD = 'workload_read_asym'
PROPERTIES = 'memcached_read_asym.properties'

def memcached_ycsb_load():
    load_cmd_tmpl = 'cd %(ycsb_dir)s; ./bin/ycsb load memcached -s ' \
        '-P %(workload)s -P %(properties)s'
    load_cmd_args = {
        'ycsb_dir': YCSB_DIR, 
        'workload': os.path.abspath(WORKLOAD),
        'properties': os.path.abspath(PROPERTIES),
    }
    load_cmd = load_cmd_tmpl % load_cmd_args
    output = subprocess.check_call(load_cmd, shell=True)
    print 'Load output:'
    print output

def memcached_ycsb_run():
    run_cmd_tmpl = 'cd %(ycsb_dir)s; ./bin/ycsb run memcached -s ' \
        '-P %(workload)s -P %(properties)s -threads 16'
    run_cmd_args = {
        'ycsb_dir': YCSB_DIR, 
        'workload': os.path.abspath(WORKLOAD),
        'properties': os.path.abspath(PROPERTIES),
    }
    run_cmd = run_cmd_tmpl % run_cmd_args
    output = subprocess.check_call(run_cmd, shell=True)
    print 'Run output:'
    print output

def main():
    # Parse arguments
    parser = argparse.ArgumentParser(description='Run YCSB for memcached for '
        'the rate-limiting and fairness experiment')
    args = parser.parse_args()

    # Connect to the remote host, configure it, and start the memcached servers
    #TODO: This can be done with paramiko and the start_memcached.py script.
    # However, I'm leaving this until later for now.

    # YCSB: Load the database
    memcached_ycsb_load()

    # YCSB: Run the benchmark
    memcached_ycsb_run()

if __name__ == '__main__':
    main()
