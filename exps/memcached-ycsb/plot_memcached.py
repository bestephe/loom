#!/usr/bin/python

import argparse
import glob
import itertools
import numpy
import os
import sys
import yaml

import matplotlib.pyplot as plt
from matplotlib.backends.backend_pdf import PdfPages
from pylab import *

import matplotlib
import matplotlib.ticker as ticker
import matplotlib.cm as mplcm
import matplotlib.colors as mplcolors
matplotlib.rcParams['ps.useafm'] = True
matplotlib.rcParams['pdf.use14corefonts'] = True
matplotlib.rcParams.update({'font.size': 14})

master_hatch = ['//', '\\\\', 'x', '+', '\\', 'o', 'O', '.', '-',  '*']

RESF = 'lines.memcached_rate.yaml'

def main():
    # Load the data and set constants
    plot_data = yaml.load(open(RESF))
    lines = plot_data['lines']
    width = 0.15
    error_kw=dict(ecolor='gray', lw=2, capsize=5, capthick=2)

    # Get the data into the right format
    labels = [l['lname'] for l in lines]
    data = [l['avg_gbps'] for l in lines]
    stddev = [l['stddev'] for l in lines]

    # Build the colormap
    color_map = get_cmap('Set1')
    c_norm = mplcolors.Normalize(vmin=0, vmax=len(lines)*1.7)
    scalar_map = mplcm.ScalarMappable(norm=c_norm, cmap=color_map)
    hatchcycle = itertools.cycle(master_hatch)
    ax = gca()
    ax.set_color_cycle([scalar_map.to_rgba(i) for i in \
        xrange(len(lines))])

    # Plot the data
    for dp_i, dp in enumerate(data):
        color = scalar_map.to_rgba(dp_i)
        rect = ax.bar(0.05 + (1.3 * dp_i * width), dp, width,
            yerr=stddev[dp_i], color=color, error_kw=error_kw)

    # Mess with axes
    yax = ax.get_yaxis()
    yax.grid(True)
    ax.set_ylabel('Throughput (Gbps)')

    # Change xticks:
    ax.set_xticks(0.05 + (width / 2.0) + (1.3 * width * np.arange(len(lines))))
    print 'labels:', labels
    ax.set_xticklabels(labels)

    # Save the figure
    #figname = 'memcached_rate.pdf'
    figname = 'memcached_rate.png'
    savefig(figname)

    show()

if __name__ == '__main__':
    main()
