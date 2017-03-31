#!/usr/bin/python

import argparse
import glob
import numpy
import os
import yaml

def memcached_parse_results(exp_prefix):
    res_files = glob.glob('results/' + exp_prefix + '*')
    if len(res_files) == 0:
        print 'Cannot find files for prefix:', exp_prefix
        return None

    gbpss = []
    for res_file in res_files:
        with open(res_file) as rf:
            data_point = yaml.load(rf)
            gbpss.append(data_point['gbps'])
    avg_gbps = 1.0 * sum(gbpss) / len(gbpss)
    stddev_gbps = numpy.asscalar(numpy.std(gbpss))

    res = {
        'avg_gbps': avg_gbps,
        'stddev': stddev_gbps,
    }
    return res

def main():
    lines = []

    prefixes_and_names = [
        # 2Gbps rate limit EXP
        ('memcached_rate.sq.2gbps_rl', 'SQ\n(2Gbps)'),
        ('memcached_rate.mq.2gbps_rl', '16Q\n(2Gbps per-Q)'),
        ('memcached_rate.mq.125mbps_rl', '16Q\n(125Mbps per-Q)'),
        ##('memcached_rate.mq.200mbps_rl', '16Q\n(200Mbps per-Q)'),
        ##('memcached_rate.mq.175mbps_rl', '16Q (175Mbps per-Q)'),

        # 4Gbps rate limit EXP
        #('memcached_rate.sq.4gbps_rl', 'SQ\n(4Gbps)'),
        #('memcached_rate.mq.4gbps_rl', '16Q\n(4Gbps per-Q)'),
        #('memcached_rate.mq.250mbps_rl', '16Q\n(250Mbps per-Q)'),
    ]
    for prefix, name in prefixes_and_names:
        line = memcached_parse_results(prefix)
        line['lname'] = name
        lines.append(line)

    plot_data = {'lines': lines}
    print yaml.dump(plot_data, default_flow_style=False)

    #fname = 'lines.memcached_4gbps_rate.yaml'
    fname = 'lines.memcached_2gbps_rate.yaml'
    with open(fname, 'w') as f:
        yaml.dump(plot_data, f, default_flow_style=False)

if __name__ == '__main__':
    main()
