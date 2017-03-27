#!/usr/bin/python

import argparse
import itertools
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
#matplotlib.rcParams['text.usetex'] = True

#matplotlib.rcParams.update({'figure.autolayout':True})
matplotlib.rcParams.update({'font.size': 11})
matplotlib.rcParams.update({'lines.linewidth': 3})

master_linestyles = ['-', '--', '-.', ':']
master_markers = ['o', 'D', 'v', '^', '<', '>', 's', 'p', '*', '+', 'x']

def plot_fairness_cdf(sq_res, mq_res):
    sq_line = sq_res['lines'][0]
    sq_line['lname'] = 'SQ'
    mq_line = mq_res['lines'][0]
    mq_line['lname'] = 'MQ'
    lines = [sq_line, mq_line]

    # Create the figure
    figure = plt.figure(figsize=(6, 2.5))
    bottom = 0.30
    legend_bbox = (0.5, -0.55)

    # Build the colormap
    color_map = get_cmap('Set1')
    c_norm = mplcolors.Normalize(vmin=0, vmax=len(lines)*1.7)
    scalar_map = mplcm.ScalarMappable(norm=c_norm, cmap=color_map)
    linescycle = itertools.cycle(master_linestyles)
    markercycle = itertools.cycle(master_markers)
    ax = gca()
    ax.set_color_cycle([scalar_map.to_rgba(i) for i in \
        xrange(len(lines))])

    # Plot the lines
    for line in lines:
        plot(line['xs'], line['ys'], label=line['lname'],
            linestyle=linescycle.next())

    # Mess with axes
    yax = ax.get_yaxis()
    yax.grid(True)
    ax.set_xlabel('FM (Gbps)')
    #ax.set_xlim(xmin=0)
    ax.set_ylabel('CDF')

    # Add the legend
    plt.legend(ncol=3, loc='lower center', bbox_to_anchor=legend_bbox,
        columnspacing=1.0, labelspacing=0.0, handletextpad=0.0,
        handlelength=1.5, frameon=False)
    plt.tight_layout()
    figure.subplots_adjust(bottom=bottom)

    # Add the title
    #title(results['title'])

def main():
    # Parse arguments
    parser = argparse.ArgumentParser(description='Plot per-job fairnes')
    parser.add_argument('--sq', help='A YAML file containing the SQ results.',
        required=True)
    parser.add_argument('--mq', help='A YAML file containing the MQ results.',
        required=True)
    parser.add_argument('--figname', help='The output name of the figure.')
    args = parser.parse_args()

    # Get the results
    with open(args.sq) as f:
        sq_res = yaml.load(f)
    with open(args.mq) as f:
        mq_res = yaml.load(f)

    # Plot the results
    plot_fairness_cdf(sq_res, mq_res)

    # Save the figure if requested
    if args.figname:
        savefig(args.figname)

    # Show the figures
    show()

if __name__ == '__main__':
    main()
