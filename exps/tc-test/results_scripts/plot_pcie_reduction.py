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

#master_hatch = ['//', '\\\\', 'x', '+', '\\', 'o', 'O', '.', '-',  '*']
master_hatch = ['//', '\\\\', 'x', '+', '\\', 'o', 'O', '.', '-',  '*']
#master_colors = ['k', 'b', 'r']

RESFS = ['results-xps/pcie-mq-reduction.yaml']

def plot_reduction(plot_data):
    lines = plot_data['lines']
    width = 0.15
    error_kw=dict(ecolor='gray', lw=2, capsize=5, capthick=2)

    # Get the data into the right format
    labels = [l['lname'] for l in lines]
    data = [l['reduction'] for l in lines]

    # Create the figure
    figure = plt.figure(figsize=(6, 2.5))
    #bottom = 0.30

    # Build the colormap
    color_map = get_cmap('Set1')
    c_norm = mplcolors.Normalize(vmin=0, vmax=len(lines)*1.7)
    scalar_map = mplcm.ScalarMappable(norm=c_norm, cmap=color_map)
    hatchcycle = itertools.cycle(master_hatch)
    #colorcycle = itertools.cycle(master_colors)
    ax = gca()
    ax.set_color_cycle([scalar_map.to_rgba(i) for i in \
        xrange(len(lines))])

    # Plot the data
    for dp_i, dp in enumerate(data):
        color = scalar_map.to_rgba(dp_i)
        #color = colorcycle.next()
        rect = ax.bar(0.05 + (1.3 * dp_i * width), dp, width,
            color=color)

    # Mess with axes
    yax = ax.get_yaxis()
    yax.grid(True)
    ax.set_ylabel('Percent Reduction\nin PCIe Writes')

    # Change xticks:
    ax.set_xticks(0.05 + (width / 2.0) + (1.3 * width * np.arange(len(lines))))
    print 'labels:', labels
    ax.set_xticklabels(labels)

    plt.tight_layout()
    #figure.subplots_adjust(bottom=bottom)

def main():
    # Parse arguments
    parser = argparse.ArgumentParser(description='Plot memcached latency')
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
    plot_reduction(results)

    # Save the figure if requested
    if args.figname:
        savefig(args.figname)

    # Show the figures
    show()

if __name__ == '__main__':
    main()
