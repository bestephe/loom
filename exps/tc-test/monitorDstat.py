#!/usr/bin/python

import argparse
import glob
import multiprocessing
import os
import re
import sys
import subprocess
import time
import yaml

def sample_dstat(runlen):
    num_cpus = multiprocessing.cpu_count()
    cpu_str = ','.join(['%d' % i for i in range(num_cpus)])
    dstat_cmd = 'dstat -c -C %s 1 %d' % (cpu_str, runlen)
    ts = time.time()
    dstat_out = subprocess.check_output(dstat_cmd, shell=True)
    lines = dstat_out.split('\n')

    headers = lines[0]
    utypes = lines[1]
    headers_split = headers.split(' ')
    utypes_split = utypes.split(':')
    assert(len(headers_split) == len(utypes_split))

    samples = []
    for report_iter, utils in enumerate(lines[2:]):
        utils_split = utils.split(':')
        if len(utils_split) != len(headers_split):
            continue
        data = {}
        for i in range(len(headers_split)):
            header = headers_split[i]
            utype = utypes_split[i]
            util_str = utils_split[i].strip()
            match = re.match(r".*-cpu(\d+).*", header)
            if match:
                cpu = 'cpu%d' % int(match.groups()[0])
            else:
                cpu = 'total'
            utype_split = re.split('\s+', utype)
            util_str_split = re.split('\s+', util_str)
            utilization = {}
            for ut_i in range(len(utype_split)):
                ut = utype_split[ut_i]
                val = util_str_split[ut_i]
                utilization[ut] = float(val)
            data[cpu] = utilization
        #data['ts'] = time.time()
        data['iter'] = report_iter + 1
        samples.append(data)
        
    return samples

def main():
    # Parse arguments
    parser = argparse.ArgumentParser(description='Use dstat to monitor system load.')
    parser.add_argument('--outf', help='The name of the output file for the samples.')
    parser.add_argument('--runlen', help='How long to monitor for (in seconds)',
                        type = float, default = 10)
    args = parser.parse_args()


    # XXX: This could be made more intelligent, but lets start simple for now.
    #  For example, this could use threading.Thread(...)
    samples = sample_dstat(args.runlen)

    # Output the data
    if args.outf:
        ofp = open(args.outf, 'w')
    else:
        ofp = sys.stdout
    yaml.dump(samples, ofp)

if __name__ == '__main__':
    main()
