#!/usr/bin/python

import argparse
import glob
import os
import platform
import subprocess
import yaml
from time import sleep
from run_memcached_ycsb import *

def memcached_config_server(config):
    #TODO: Configure the number of NIC queues

    #TODO: Configure Qdisc/TC

    #TODO: Configure XPS
    pass

def memcached_start_servers(config):
    #XXX: "-L" enables hugepage support.  This currently didn't work for me.
    #cmd_tmpl = '/usr/bin/memcached -m 1024 -p %(port)s -u memcache -t 1 -d -L'
    cmd_tmpl = '/usr/bin/memcached -m 1024 -p %(port)s -u memcache -t 4 -d'
    procs = []
    for server_conf in config.servers:
        cmd = cmd_tmpl % {'port': server_conf.port}
        taskset_tmpl = 'sudo taskset -c %(cpu)s %(cmd)s'
        taskset_cmd = taskset_tmpl % {'cpu': server_conf.cpu, 'cmd': cmd}
        proc = subprocess.Popen(taskset_cmd, shell=True,
            stderr=subprocess.STDOUT, stdout=subprocess.PIPE)
        procs.append(proc)
    return procs

def memcached_kill_servers(config):
    #XXX: WARNING: This will kill all memcached processes running
    kill_cmd = 'sudo killall memcached'
    subprocess.call(kill_cmd, shell=True)

def main():
    # Parse arguments
    parser = argparse.ArgumentParser(description='Configure the server and '
        'start memcached for the rate-limiting and fairness experiment')
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

    # Kill all old servers
    memcached_kill_servers(config)

    # Configure the server
    memcached_config_server(config)

    # Start the memcached servers
    procs = memcached_start_servers(config)

    #XXX: DEBUG
    #for proc in procs:
    #    print proc.stdout.read()

if __name__ == '__main__':
    main()
