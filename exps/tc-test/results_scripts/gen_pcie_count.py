#!/usr/bin/python

import argparse
import numpy
import os
import re
import scipy
import scipy.stats
import sys
import yaml

def get_percentile(data, percentile):
    return numpy.asscalar(scipy.stats.scoreatpercentile(data, percentile))

def get_percent_increase(loom, current):
    return 100 * ((1.0 * current / loom) - 1)

def get_percent_reduction(loom, current):
    return 100 * (1.0 - (1.0 * loom / current))

def gen_pcie_counts(results):
    counts = {'loom': 0, 'current': 0}
    ethres = results['ethtool']
    for txq in ethres:
        counts['loom'] += ethres[txq]['no_xmit_more']
        counts['current'] += ethres[txq]['change_sk']

    #print yaml.dump(ethres)

    counts['reduction'] = get_percent_reduction(counts['loom'], counts['current'])
    counts['increase'] = get_percent_reduction(counts['loom'], counts['current'])

    # Get writes per ms
    counts['loom'] = counts['loom'] * 1.0 / 20.0 / 1000.0
    counts['current'] = counts['current'] * 1.0 / 20.0 / 1000.0

    return counts

def main():
    # Parse arguments
    parser = argparse.ArgumentParser(description='Generate latency lines')
    parser.add_argument('--results', help='A list of YAML files containing the '
        'ethtool results.', nargs='+', required=True)
    parser.add_argument('--outf', help='The output file.')
    args = parser.parse_args()

    # Generate the counts
    tmp_res = {}
    for resfname in args.results:
        with open(resfname) as resultf:
            expres = yaml.load(resultf)
            numapps = len(expres) - 1
            numconns = 8
            match = re.match(r".*_(\d+)conn.*", resfname)
            if match:
                numconns = int(match.groups()[0]) 
            counts = gen_pcie_counts(expres)
            exp_str = '%d Tenant\n%d Conn' % (numapps, numconns)
            if exp_str not in tmp_res:
                tmp_res[exp_str] = []
            #print exp_str, 'current:', counts['current']
            print exp_str, yaml.dump(counts)
            tmp_res[exp_str].append(counts['reduction'])

    numapps = tmp_res.keys()
    numapps.sort()
    lines = []
    for exp_str in numapps:
        med_reduction = get_percentile(tmp_res[exp_str], 50)
        line = {'lname': exp_str, 'reduction': med_reduction}
        lines.append(line)
    results = {'lines': lines}

    # Output the results
    if args.outf:
        with open(args.outf, 'w') as f:
            yaml.dump(results, f)
    else:
        print yaml.dump(results)

if __name__ == '__main__':
    main()
