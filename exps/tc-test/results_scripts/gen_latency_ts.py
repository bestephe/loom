#!/usr/bin/python

import argparse
import numpy
import os
import scipy
import scipy.stats
import sys
import yaml

TIME_SCALE = 0.1
PTILE = 50

def get_percentile(data, percentile):
    return numpy.asscalar(scipy.stats.scoreatpercentile(data, percentile))

def gen_latency_line(xs, ys, time_scale, ptile):
    new_xs, new_ys = [], []
    cur_ts = xs[0]
    agg_ys = []
    assert(len(xs) == len(ys))
    for i in range(len(xs)):
        if xs[i] > cur_ts + TIME_SCALE:
            new_xs.append(cur_ts)
            new_y = get_percentile(agg_ys, ptile)
            new_ys.append(new_y)
            cur_ts = xs[i]
            agg_ys = []
        agg_ys.append(ys[i])
    return {'xs': new_xs, 'ys': new_ys}

def main():
    # Parse arguments
    parser = argparse.ArgumentParser(description='Generate latency lines')
    parser.add_argument('--sq', help='A YAML file containing the SQ results.')
    parser.add_argument('--mq', help='A YAML file containing the MQ results.')
    parser.add_argument('--loom', help='A YAML file containing the MQ-Pri results.')
    parser.add_argument('--outf', help='The output file.')
    args = parser.parse_args()

    # Generate the lines
    lines = []
    line_data = [('SQ', args.sq), ('MQ', args.mq), ('Loom', args.loom)]
    for line_name, linefname in line_data:
        if not linefname:
            continue
        with open(linefname) as linef:
            line_res = yaml.load(linef)
            spres = line_res['sockperf_0']['samples']
            xs = spres['xs']
            ys = spres['ys']
            line = gen_latency_line(xs, ys, TIME_SCALE, PTILE)
            line['lname'] = line_name
            #line = {'lname': line_name, 'xs': xs, 'ys': ys}
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
