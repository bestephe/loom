#!/usr/bin/python

import argparse
import glob
import os
import platform
import subprocess
import yaml
from time import sleep

MEMCACHED_CONFIG_DEFAULTS = {
    'max_mem': (16 * 1024),
    'threads': 10,
    'high_prio': True,

    #XXX: Not really used anymore
    'servers': [
        {'cpu': 0, 'port': 11212},
        #{'cpu': 4, 'port': 11214},
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

def memcached_start_servers(config):
    #XXX: "-L" enables hugepage support.  This currently didn't work for me.
    cmd_tmpl = '/usr/bin/memcached -m %(max_mem)d -p %(port)s -u memcache -t %(threads)s -R 1000 -d -c 4096'

    procs = []
    for server_conf in config.servers:
        cmd = cmd_tmpl % {'max_mem': config.max_mem, 'port': server_conf.port,
            'threads': config.threads}

        #XXX: Don't pin cores for now
        #taskset_tmpl = 'sudo taskset -c %(cpu)s %(cmd)s'
        #taskset_cmd = taskset_tmpl % {'cpu': server_conf.cpu, 'cmd': cmd}

        # Run in a container to set network priority if necessary
        if config.high_prio:
            cg_cmd = 'sudo cgexec -g net_prio:high_prio %(cmd)s'
        else:
            cg_cmd = 'sudo %(cmd)s'
        cg_cmd = cg_cmd % {'cmd': cmd}
        print 'cg_cmd:', cg_cmd

        proc = subprocess.Popen(cg_cmd, shell=True,
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
    #XXX: This could be in the config.  But currently the config isn't used.
    parser.add_argument('--high-prio', help='Start the memcached process '
        'at a high network priority. Overrides the config', action='store_true')
    args = parser.parse_args()

    # Get the config
    if args.config:
        with open(args.config) as configf:
            user_config = yaml.load(configf)
        config = MemcachedConfig(user_config)
    else:
        config = MemcachedConfig()

    # Override the priority if specified
    if args.high_prio:
        config.high_prio = args.high_prio

    # Kill all old servers
    memcached_kill_servers(config)
    sleep(0.2)

    # Start the memcached servers
    procs = memcached_start_servers(config)

    #XXX: DEBUG
    for proc in procs:
        print proc.stdout.read()

if __name__ == '__main__':
    main()
