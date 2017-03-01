#!/usr/bin/python

import argparse
import glob
import os
import platform
import subprocess
import yaml
from time import sleep
from start_memcached import *

YCSB_DIR = '/scratch/bes/git/YCSB/'

QMODEL_SQ = 'sq'
QMODEL_MQ = 'mq'

MEMCACHED_CONFIG_DEFAULTS = {
    'qmodel': QMODEL_MQ,
    'rate_limit': 5e9,
    'workload': 'workload_read_asym',
    'properties': 'memcached_read_asym.properties',
    'servers': [
        {'cpu': 0, 'port': 11212},
        {'cpu': 4, 'port': 11214},
    ],
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

class MemcachedConfig(object):
    def __init__(self, *initial_data, **kwargs):
        for key in MEMCACHED_CONFIG_DEFAULTS:
            setattr(self, key, MEMCACHED_CONFIG_DEFAULTS[key])
        for dictionary in initial_data:
            for key in dictionary:
                if not hasattr(self, key):
                    print 'WARNING! Unexpected attr: %s' % key
                setattr(self, key, dictionary[key])
        for key in kwargs:
            if not hasattr(self, key):
                print 'WARNING! Unexpected attr: %s' % key
            setattr(self, key, kwargs[key])
        # Parse the server config
        self.servers = [ServerConfig(s) for s in self.servers]
    def dump(self):
        d = self.__dict__.copy()
        d['servers'] = d['servers'].dump()
        return d

def memcached_ycsb_load(config):
    load_cmd_tmpl = 'cd %(ycsb_dir)s; ./bin/ycsb load memcached -s ' \
        '-P %(workload)s -P %(properties)s'
    load_cmd_args = {
        'ycsb_dir': YCSB_DIR, 
        'workload': os.path.abspath(config.workload),
        'properties': os.path.abspath(config.properties),
    }
    load_cmd = load_cmd_tmpl % load_cmd_args
    output = subprocess.check_call(load_cmd, shell=True)
    print 'Load output:'
    print output

def memcached_ycsb_run(config):
    run_cmd_tmpl = 'cd %(ycsb_dir)s; ./bin/ycsb run memcached -s ' \
        '-P %(workload)s -P %(properties)s -threads 16'
    run_cmd_args = {
        'ycsb_dir': YCSB_DIR, 
        'workload': os.path.abspath(config.workload),
        'properties': os.path.abspath(config.properties),
    }
    run_cmd = run_cmd_tmpl % run_cmd_args
    output = subprocess.check_call(run_cmd, shell=True)
    print 'Run output:'
    print output

def main():
    # Parse arguments
    parser = argparse.ArgumentParser(description='Run YCSB for memcached for '
        'the rate-limiting and fairness experiment')
    parser.add_argument('--config', help='An alternate configuration file. '
        'The configuration format is unsurprisingly not documented.')
    args = parser.parse_args()

    # Get the config
    if args.config:
        with open(args.config) as configf:
            user_config = yaml.load(configf)
        config = MemcachedConfig(user_config)
    else:
        config = MemcachedConfig()

    # Connect to the remote host, configure it, and start the memcached servers
    #TODO: This can be done with paramiko and the start_memcached.py script.
    # However, I'm leaving this until later for now.

    # YCSB: Load the database
    memcached_ycsb_load(config)

    # YCSB: Run the benchmark
    memcached_ycsb_run(config)

if __name__ == '__main__':
    main()
