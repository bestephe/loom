#!/usr/bin/python

import argparse
import numpy as np
import os
import re
import scipy.stats
import sys
import yaml

JOB_FAIR_RATIO = 3

def get_percentile(data, percentile):
    return np.asscalar(scipy.stats.scoreatpercentile(data, percentile))

def compute_fm(args):
    fms = []
    for tsfname in args.tsfiles:
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
            j1_tput = []
            j2_tput = []
            for line in lines:
                if line['lname'] == 'Total':
                    tot_tput = line['ys']
                elif line['lname'] == 'Job1':
                    j1_tput = line['ys']
                elif line['lname'] == 'Job2':
                    j2_tput = line['ys']
            assert(len(tot_tput) == len(xs))
            assert(len(j1_tput) == len(xs))
            assert(len(j2_tput) == len(xs))

            # Compute the FM only when the total throughput is high enough and
            # both jobs are active
            for i in range(len(xs)):
                if tot_tput[i] > 8.5 and j1_tput[i] > 1 and j2_tput[i] > 1:
                    fs_j1 = 1.0 * j1_tput[i] / JOB_FAIR_RATIO
                    fs_j2 = j2_tput[i]
                    fm = abs(fs_j1 - fs_j2)
                    fms.append(fm)

    return fms

def gen_cdf(fms):
    counts, edges = np.histogram(fms, bins=500, normed=True)
    cdf = np.cumsum(counts)
    xs = edges[:-1]
    xs = xs.tolist()
    ys = [np.asscalar(x*(edges[1]-edges[0])) for x in cdf]


    lines = [{'lname': 'FM-CDF', 'xs': xs, 'ys': ys}]
    results = {'lines': lines}

    return results
    

def main():
    # Parse arguments
    parser = argparse.ArgumentParser(description='Go from the per-job '
        'throughput timeseries to a fairness metric (FM) CDF.')
    # TODO: I could just take in a list of files instead
    parser.add_argument('tsfiles', help='The files to parse.',
        nargs='+')
    parser.add_argument('--outf', help='The output file.')
    args = parser.parse_args()

    # Compute the fairness metric (FM)
    fms = compute_fm(args)

    # Get a CDF of the results
    cdf_res = gen_cdf(fms)

    # Output the results
    if args.outf:
        with open(args.outf, 'w') as f:
            yaml.dump(cdf_res, f)
    else:
        pass
        #print yaml.dump(cdf_res)

    print 'median:', get_percentile(fms, 50)
    print '75p:', get_percentile(fms, 75)
    print '90p:', get_percentile(fms, 90)

if __name__ == "__main__":
    main()
