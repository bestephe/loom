#!/usr/bin/python

import argparse
import glob
import numpy
import os
import yaml

def memcached_parse_results(exp_prefix):
    res_files = glob.glob('results/' + exp_prefix + '*')
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

    # Get the SQ results
    sq_prefix = 'memcached_rate.sq.2gbps_rl'
    sq_results = memcached_parse_results(sq_prefix)
    sq_results['lname'] = 'SQ'
    lines.append(sq_results)

    # Get the MQ 2Gbps results
    mq_2g_prefix = 'memcached_rate.mq.2gbps_rl'
    mq_2g_results = memcached_parse_results(mq_2g_prefix)
    mq_2g_results['lname'] = '8Q (2Gbps per-Q)'
    lines.append(mq_2g_results)

    # Get the MQ 250Mbps results
    mq_250m_prefix = 'memcached_rate.mq.250mbps_rl'
    mq_250m_results = memcached_parse_results(mq_250m_prefix)
    mq_250m_results['lname'] = '8Q (250Mbps per-Q)'
    lines.append(mq_250m_results)

    plot_data = {'lines': lines}
    print yaml.dump(plot_data, default_flow_style=False)

    fname = 'lines.memcached_rate.yaml'
    with open(fname, 'w') as f:
        yaml.dump(plot_data, f, default_flow_style=False)

if __name__ == '__main__':
    main()
