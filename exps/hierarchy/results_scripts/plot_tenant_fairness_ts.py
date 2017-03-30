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

def plot_job_tput(results):
    lines = results['lines']

    # Create the figure
    figure = plt.figure(figsize=(6, 2.5))
    bottom = 0.45
    legend_bbox = (0.5, -0.5)

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
        if line['lname'] == 'Total':
            continue
        plot(line['xs'], line['ys'], label=line['lname'],
            linestyle=linescycle.next())

    # Mess with axes
    yax = ax.get_yaxis()
    yax.grid(True)
    ax.set_xlabel('Time (seconds)')
    #ax.set_xlim(xmin=0)
    ax.set_ylabel('Throughput (Gbps)')
    figure.subplots_adjust(bottom=bottom)

    # Add the legend
    plt.legend(ncol=3, loc='lower center', bbox_to_anchor=legend_bbox,
        columnspacing=1.0, labelspacing=0.0, handletextpad=0.0,
        handlelength=1.5, frameon=False)
    plt.tight_layout()

    # Add the title
    title(results['title'])

def main():
    # Parse arguments
    parser = argparse.ArgumentParser(description='Plot per-job fairnes')
    parser.add_argument('--results', help='A YAML file containing the results.',
        required=True)
    parser.add_argument('--figname', help='The output name of the figure.')
    args = parser.parse_args()

    # Get the results
    with open(args.results) as f:
        results = yaml.load(f)

    # Add a title if there is none
    if 'title' not in results:
        results['title'] = args.results

    # Plot the results
    plot_job_tput(results)

    # Save the figure if requested
    if args.figname:
        savefig(args.figname)

    # Show the figures
    show()

if __name__ == '__main__':
    main()
