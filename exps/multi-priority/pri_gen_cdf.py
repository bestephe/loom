#!/usr/bin/python

import argparse
import numpy as np
import os
import re
import scipy.stats
import sys
import yaml

def get_percentile(data, percentile):
    return np.asscalar(scipy.stats.scoreatpercentile(data, percentile))

def get_data_points(files):
    data_points = []
    for tsfname in files:
        with open(tsfname) as tsf:
            tsdata = yaml.load(tsf)
            xs = tsdata['latencies'].keys()
            xs.sort()
            #ys = [tsdata['latencies'][x]['avg'] for x in xs]
            ys = [tsdata['latencies'][x]['90p'] for x in xs]
            data_points.extend(ys)

    return data_points

def gen_cdf(data_points):
    counts, edges = np.histogram(data_points, bins=500, normed=True)
    cdf = np.cumsum(counts)
    xs = edges[:-1]
    xs = xs.tolist()
    ys = [np.asscalar(x*(edges[1]-edges[0])) for x in cdf]

    line = {'lname': 'Latency-CDF', 'xs': xs, 'ys': ys}

    return line

def main():
    # Parse arguments
    parser = argparse.ArgumentParser(description='Go from the per-run '
        'latency timeseries to a latency CDF.')
    parser.add_argument('--sq', help='The SQ files to parse.',
        nargs='+', required=True)
    parser.add_argument('--mq', help='The MQ files to parse.',
        nargs='+', required=True)
    parser.add_argument('--mq-pri', help='The MQ-Pri files to parse.',
        nargs='+', required=True)
    # TODO: what percentile to take a CDF of.  Currently only uses average.
    parser.add_argument('--outf', help='The output file.')
    args = parser.parse_args()

    files_and_names = [(args.sq, 'SQ'), (args.mq, 'MQ'),
        (args.mq_pri, 'MQ-Pri')]
    lines = []
    for (files, lname) in files_and_names:
        # Get the data points
        data_points = get_data_points(files)

        # Get a CDF of the results
        line = gen_cdf(data_points)
        line['lname'] = lname
        lines.append(line)

    # Skip additional metadata for now
    results = {'lines': lines}

    print yaml.dump(results)

    # Output the results
    if args.outf:
        with open(args.outf, 'w') as f:
            yaml.dump(results, f)

if __name__ == "__main__":
    main()
