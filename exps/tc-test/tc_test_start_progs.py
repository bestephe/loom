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

PROG_DIR_SRC = 'src'
PROG_DIR_SINK = 'sink'
PROG_DIRS = [PROG_DIR_SRC, PROG_DIR_SINK]

def main():
    parser = argparse.ArgumentParser(description='Run exps from saved configs.')
    parser.add_argument('--dir', help='The app direction to start.',
        required=True, choices=PROG_DIRS)
    parser.add_argument('--configf', help='The YAML config for the experiment.',
        required=True)
    parser.add_argument('--extra-name', help='An extra string to add to the experiment name.',
        default='')
    parser.add_argument('--run', help='The number of this run.', default=1, type=int)
    args = parser.parse_args()

    with open(args.configf) as cf:
        cdict = yaml.load(cf)
        tc_config = TcTestConfig(cdict)
        tc_config.run = args.run
        tc_config.extra_name = args.extra_name

        print 'Read YAML Config:', tc_config.dump()

        # Start by killing all existing programs
        tc_test_killall()

        # Start the programs
        apps = []
        for app_conf in tc_config.apps:
            if args.dir == PROG_DIR_SRC:
                app = tc_test_start_src(app_conf)
            elif args.dir == PROG_DIR_SINK:
                app = tc_test_start_sink(app_conf)
            else:
                raise ValueError("Unexpected dir")
            apps.append(app)

        # Wait for the app to finish
        #prog.proc.wait()

        # Parse output
        results = {}
        for app in apps:
            if args.dir == PROG_DIR_SRC:
                output = app.parse_src_output()
            elif args.dir == PROG_DIR_SINK:
                output = app.proc.stdout.read()
            else:
                raise ValueError("Unexpected dir")
            results[app.pconf.name] = output

        # Get ethtool output if requested
        if tc_config.ethtool:
            ethtool_out = parse_ethtool_output()
            results['ethtool'] = ethtool_out

        # Save the output for the src
        if args.dir == PROG_DIR_SRC:
            exp_str = tc_config.get_exp_str()
            outfname = 'results/src.%s' % exp_str
            with open(outfname, 'w') as outf:
                yaml.dump(results, outf, default_flow_style=False)

        # TODO: Change: Less magic 
        avg_agg = 0
        for appn in results:
            app_res = results[appn]
            if 'agg_tput' in app_res:
                avg_agg += app_res['agg_tput']['avg']
        results['avg_agg'] = avg_agg

        # Print output
        print yaml.dump(results)

if __name__ == '__main__':
    main()
