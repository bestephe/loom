#!/usr/bin/python

import argparse
import numpy as np
import os
import re
import scipy.stats
import sys
import yaml

FLOAT_DESC_STR = '[-+]?(\d+(\.\d*)?|\.\d+)([eE][-+]?\d+)?'

def get_percentile(data, percentile):
    return np.asscalar(scipy.stats.scoreatpercentile(data, percentile))

def parse_files(files):
    cts = []
    for fname in files:
        with open(fname) as resf:
            lines = resf.readlines()
            finish_ts = 0
            for line in lines:
                match = re.match(".*Job.*finished.* (%s) s.*" % FLOAT_DESC_STR, line)
                if match:
                    ts = float(match.groups()[0])
                    cts.append(ts)

                    # First job only
                    break

                    finish_ts = max(finish_ts, ts)
                    #print 'new finish_ts:', finish_ts
            #print
            #cts.append(finish_ts)
    cts.sort()
    print yaml.dump(cts)
    return cts

def main():
    # Parse arguments
    parser = argparse.ArgumentParser(description='Parse the output of the '
        'Spark fairness experiments.')
    parser.add_argument('files', help='The output of the spark fairness runs.',
        nargs='+')
    args = parser.parse_args()

    # Parse the files
    completion_times = parse_files(args.files)
    print 'Num results:', len(completion_times)
    print 'avg:', (1.0 * sum(completion_times) / len(completion_times))
    print 'median:', get_percentile(completion_times, 50)
    print '75p:', get_percentile(completion_times, 75)
    print '90p:', get_percentile(completion_times, 90)


if __name__ == '__main__':
    main()
