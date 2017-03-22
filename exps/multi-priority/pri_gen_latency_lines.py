#!/usr/bin/python

import argparse
import numpy
import os
import sys
import yaml

def gen_latency_line(results, ptile):
    xs = results['latencies'].keys()
    xs.sort()
    ys = [results['latencies'][x][ptile] for x in xs]
    return {'xs': xs, 'ys': ys}

def main():
    # Parse arguments
    parser = argparse.ArgumentParser(description='Generate latency lines')
    parser.add_argument('--sq', help='A YAML file containing the SQ results.',
        required=True)
    parser.add_argument('--mq', help='A YAML file containing the MQ results.',
        required=True)
    parser.add_argument('--ptile', help='What percentile (or avg) to use.',
        choices=('avg', '90p', '99p'), default='90p')
    parser.add_argument('--outf', help='The output file.')
    args = parser.parse_args()

    # Get the results
    with open(args.sq) as f:
        sq_res = yaml.load(f)
    with open(args.mq) as f:
        mq_res = yaml.load(f)

    # Generate the lines
    sq_line = gen_latency_line(sq_res, args.ptile)
    sq_line['lname'] = 'SQ'
    mq_line = gen_latency_line(mq_res, args.ptile)
    mq_line['lname'] = 'MQ'
    lines = {'lines': [sq_line, mq_line]}

    # Output the results
    if args.outf:
        with open(args.outf, 'w') as f:
            yaml.dump(lines, f)
    else:
        print yaml.dump(lines)

if __name__ == '__main__':
    main()
