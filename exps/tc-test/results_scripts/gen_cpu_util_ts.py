#!/usr/bin/python

import argparse
import numpy
import os
import scipy
import scipy.stats
import sys
import yaml

TIME_SCALE = 0.25
PTILE = 90

def get_percentile(data, percentile):
    return numpy.asscalar(scipy.stats.scoreatpercentile(data, percentile))

def gen_cpu_util_line(runs, ptile):
    print yaml.dump(runs)
    comb_ys = zip(*[run[1] for run in runs])
    print comb_ys
    avg_ys = map(lambda y: 1.0 * sum(y) / len(y), comb_ys)
    ptile_ys = map(lambda y: get_percentile(y, PTILE), comb_ys)
    print yaml.dump(avg_ys)
    return (runs[0][0], avg_ys)
    #return (runs[0][0], ptile_ys)

def main():
    # Parse arguments
    parser = argparse.ArgumentParser(description='Generate latency lines')
    parser.add_argument('--sq', nargs='+', help='A YAML file containing the SQ results.')
    parser.add_argument('--mq', nargs='+', help='A YAML file containing the MQ results.')
    parser.add_argument('--qpf', nargs='+', help='A YAML file containing the QPF results.')
    parser.add_argument('--loom', nargs='+', help='A YAML file containing the Loom results.')
    parser.add_argument('--outf', help='The output file.')
    args = parser.parse_args()

    # Generate the lines
    lines = []
    line_data = [('SQ', args.sq), ('MQ', args.mq), ('QPF', args.qpf), ('Loom', args.loom)]
    for line_name, linefnames in line_data:
        if not linefnames:
            continue
        runs = []
        for linefname in linefnames: 
            try:
                with open(linefname) as linef:
                    line_res = yaml.load(linef)
                    ivals = line_res['dstat']['ivals']
                    xs = range(len(ivals))
                    ys = ivals
                    ys = [(y - 200) / 100.0 for y in ys]
                    runs.append((xs, ys))
            except:
                print 'Bad results:', linefname
                continue
        xs, ys = gen_cpu_util_line(runs, PTILE)
        line = {'lname': line_name, 'xs': xs, 'ys': ys}
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
