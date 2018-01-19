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

def tc_test_setup_sink(sink_ssh, cfname):
    cmd_dir = LOOM_HOME + '/exps/tc-test/'
    #cf_abspath = os.path.abspath(cfname)
    cmd = 'sudo ./tc_test_start_progs.py --dir sink --configf %s' % cfname
    sink_cmd = 'cd %s; %s' % (cmd_dir, cmd)
    stdin, stdout, stderr = sink_ssh.exec_command(sink_cmd)
    return (stdin, stdout, stderr)

def tc_test_setup_src(src_ssh, cfname):
    cmd_dir = LOOM_HOME + '/exps/tc-test/'
    #cf_abspath = os.path.abspath(cfname)
    cmd = 'sudo ./tc_test_start_progs.py --dir src --configf %s' % cfname
    src_cmd = 'cd %s; %s' % (cmd_dir, cmd)
    stdin, stdout, stderr = src_ssh.exec_command(src_cmd)
    return (stdin, stdout, stderr)

def main():
    parser = argparse.ArgumentParser(description='Run exps from saved configs.')
    parser.add_argument('--configs', help='A list of YAML configs.',
        required=True, nargs='+')
    parser.add_argument('--runs', help='The number of runs to do.', default=1, type=int)
    args = parser.parse_args()

    for ci, cfname in enumerate(args.configs):
        with open(cfname) as cf:
            cdict = yaml.load(cf)
            tc_config = TcTestConfig(cdict)
            print 'config (%d/%d):' % (ci, len(args.configs))
            for i in range(1, args.runs+1):
                tc_config.run = i


                #TODO: I'd be happier if this was done via some RPC framework
                # than natively over paramiko + NFS
                sink_ssh = connect_rhost(tc_config.sink_host)
                #TODO: waiting would be unecessary with better RPC
                src_ssh = connect_rhost(tc_config.src_host)

                sink_proc = tc_test_setup_sink(sink_ssh, cfname)
                sleep(1) # Let the sinks start
                src_proc = tc_test_setup_src(src_ssh, cfname)

                #sink_stdout, sink_stderr = sink_proc[1], sink_proc[2]
                #print 'sink_stdout:', sink_stdout.read()
                #print 'sink_stderr:', sink_stderr.read()

                src_stdout, src_stderr = src_proc[1], src_proc[2]
                print 'src_stdout:', src_stdout.read()
                print 'src_stderr:', src_stderr.read()

if __name__ == '__main__':
    main()
