#!/usr/bin/python

import argparse
import glob
import os
import platform
import subprocess
import yaml
from time import sleep

QMODEL_SQ = 'sq'
QMODEL_MQ = 'mq'

MEMCACHED_CONFIG_DEFAULTS = {
    'qmodel':       QMODEL_MQ,
    'rate_limit': 5e9,
    'servers': [
        {'cpu': 0, 'port': 11212},
        {'cpu': 4, 'port': 11213},
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

def memcached_config_server(config):
    #TODO: Configure the number of NIC queues

    #TODO: Configure Qdisc/TC

    #TODO: Configure XPS
    pass

def memcached_start_servers(config):
    #XXX: "-L" enables hugepage support.  This currently didn't work for me.
    #cmd_tmpl = '/usr/bin/memcached -m 1024 -p %(port)s -u memcache -t 1 -d -L'
    cmd_tmpl = '/usr/bin/memcached -m 1024 -p %(port)s -u memcache -t 1 -d'
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
        'start memcached for the rate-limiting and fairness experimentq')
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
