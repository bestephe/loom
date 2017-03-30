#!/usr/bin/python

import argparse
import numpy as np
import os
import re
import scipy.stats
import sys
import yaml

#JOB_FAIR_RATIO = 3

def get_percentile(data, percentile):
    return np.asscalar(scipy.stats.scoreatpercentile(data, percentile))

def compute_fm(files):
    fms = []
    for tsfname in files:
        with open(tsfname) as tsf:
            tsdata = yaml.load(tsf)
            lines = tsdata['lines'] 

            # Get the xs and sanity check
            xs = lines[0]['xs']
            for line in lines:
                assert(xs == line['xs'])
                assert(len(line['xs']) == len(line['ys']))

            # Get the total tput and the tput of J1 and J2
            tot_tput = []
            t1_tput = []
            t2_tput = []
            for line in lines:
                if line['lname'] == 'Total':
                    tot_tput = line['ys']
                elif line['lname'] == 'Tenant1':
                    t1_tput = line['ys']
                elif line['lname'] == 'Tenant2':
                    t2_tput = line['ys']
            assert(len(tot_tput) == len(xs))
            assert(len(t1_tput) == len(xs))
            assert(len(t2_tput) == len(xs))

            # Compute the FM only when the total throughput is high enough and
            # both jobs are active
            for i in range(len(xs)):
                if tot_tput[i] > 8.25 and t1_tput[i] > 0.5 and t2_tput[i] > 0.5:
                    fs_t1 = t1_tput[i]
                    fs_t2 = t2_tput[i]
                    fm = abs(fs_t1 - fs_t2)
                    fms.append(fm)

    return fms

def gen_cdf(fms):
    counts, edges = np.histogram(fms, bins=500, normed=True)
    cdf = np.cumsum(counts)
    xs = edges[:-1]
    xs = xs.tolist()
    ys = [np.asscalar(x*(edges[1]-edges[0])) for x in cdf]

    # Avoid lines starting at earlier times
    xs = [xs[0] - 0.001] + xs
    ys = [0] + ys

    line = {'lname': 'FM-CDF', 'xs': xs, 'ys': ys}
    return line
    

def main():
    # Parse arguments
    parser = argparse.ArgumentParser(description='Go from the per-job '
        'throughput timeseries to a fairness metric (FM) CDF.')
    parser.add_argument('--sq', help='The SQ files to parse.',
        nargs='+', required=True)
    parser.add_argument('--mq', help='The MQ files to parse.',
        nargs='+', required=True)
    parser.add_argument('--mq-pri', help='The MQ-Pri files to parse.',
        nargs='+', required=True)
    parser.add_argument('--mq-ets', help='The MQ-ets files to parse.',
        nargs='+', required=True)
    parser.add_argument('--outf', help='The output file.')
    args = parser.parse_args()

    files_and_names = [(args.sq, 'SQ'), (args.mq, 'MQ'),
        (args.mq_pri, 'MQ-Pri'), (args.mq_ets, 'MQ-ETS')]
    lines = []
    for (files, lname) in files_and_names:
        # Compute the fairness metric (FM)
        fms = compute_fm(files)

        # Get a CDF of the results
        line = gen_cdf(fms)
        line['lname'] = lname
        lines.append(line)
    results = {'lines': lines}

    # Output the results
    if args.outf:
        with open(args.outf, 'w') as f:
            yaml.dump(results, f)
    else:
        pass
        #print yaml.dump(cdf_res)

if __name__ == "__main__":
    main()
