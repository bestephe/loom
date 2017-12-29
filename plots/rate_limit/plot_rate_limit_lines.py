#!/usr/bin/python

import argparse
import itertools
import os
import sys
import yaml

#sys.path.insert(0, os.path.abspath('..'))
#from loom_plt_common import *

import matplotlib.pyplot as plt
from matplotlib.backends.backend_pdf import PdfPages
from pylab import *

import matplotlib
import matplotlib.ticker as ticker
import matplotlib.cm as mplcm
import matplotlib.colors as mplcolors
matplotlib.rcParams['ps.useafm'] = True
matplotlib.rcParams['pdf.use14corefonts'] = True

# Default config
RATE_PLT_CONFIG_DEFAULTS = {
    # Experiment naming config
    'expname': None,
    'outf': None,
    'lines': None,

    # Labels
    'title': '',
    'xlabel': 'Rate-limit Update Time ($\mu$s)',
    'ylabel': 'CDF',

    # Matplotlib config
    'xticks': None,
    'xmin': 0.5e-6,
    'ymin': None,
    'ymax': 1.0,
    'legend_width': 3,
}

params = {#'text.usetex': True,
    'font.size' : 18,
    #'title.fontsize' : 14,
    'axes.labelsize': 20,
    'text.fontsize' : 20,
    'xtick.labelsize' : 20,
    'ytick.labelsize' : 20,
    #'legend.fontsize' : 'medium',
    'legend.fontsize' : '18',
    'lines.linewidth' : 6,
    'lines.markersize' : 6,
}
rcParams.update(params)

master_linestyles = ['-', '--', '-.', ':']
master_markers = ['o', 'D', 'v', '^', '<', '>', 's', 'p', '*', '+', 'x']
master_hatch = ['+', 'x', '\\', 'o', 'O', '.', '-',  '*']

def plot_line_data(plot_data):
    if not hasattr(plot_data, 'bottom'):
        plot_data.bottom = 0.35
    if not hasattr(plot_data, 'legend_y'):
        plot_data.legend_y = -0.6

    # Setup the figure
    f = figure(figsize=(8, 4))
    legend_bbox = (0.45, plot_data.legend_y)
    legend_width = plot_data.legend_width

    # Build the colormap
    color_map = get_cmap('Set1')
    #c_norm = mplcolors.Normalize(vmin=0, vmax=len(plot_data.lines)*2)
    #color_map = get_cmap('gist_stern')
    #color_map = get_cmap('Dark2')
    #color_map = get_cmap('gnuplot')
    #color_map = get_cmap('nipy_spectral')
    c_norm = mplcolors.Normalize(vmin=0, vmax=len(plot_data.lines)*1.7)
    scalar_map = mplcm.ScalarMappable(norm=c_norm, cmap=color_map)
    linescycle = itertools.cycle(master_linestyles)
    markercycle = itertools.cycle(master_markers)

    ax = gca()
    ax.set_color_cycle([scalar_map.to_rgba(i) for i in \
        xrange(len(plot_data.lines))])

    # Plot the data
    for line in plot_data.lines:
        plot(line.xs, line.ys, label=line.lname, linestyle=linescycle.next(),
             marker=markercycle.next())

    # Mess with axes
    yax = ax.get_yaxis()
    yax.grid(True)
    ax.set_xlabel(plot_data.xlabel)
    if plot_data.xmin:
        ax.set_xlim(xmin=plot_data.xmin)
    ax.set_xscale("log", nonposx='clip')
    ax.set_ylabel(plot_data.ylabel)

    # Change xticks:
    if plot_data.xticks:
        print 'xticks!', tuple(plot_data.xticks)
        _ret = xticks(np.arange(len(plot_data.xticks)), tuple(plot_data.xticks))
        #f.autofmt_xdate()
        print _ret

    # Change limits
    if plot_data.ymin != None:
        ylim(ymin = plot_data.ymin)
    if plot_data.ymax != None:
        ylim(ymax = plot_data.ymax)

    #title(get_title(outfname))
    title(plot_data.title, fontsize=12)

    # Add legend
    #ax.legend(loc='lower center', bbox_to_anchor=legend_bbox, ncol=legend_width, columnspacing=0.5, handletextpad=0.25, fancybox=True, shadow=True)
    #plt.tight_layout()
    #f.subplots_adjust(hspace=0.25, wspace=0.185, left=0.20, bottom=plot_data.bottom)

    # Alternate legend
    plt.legend(ncol=1, loc='lower left',
        columnspacing=1.0, labelspacing=0.0, handletextpad=0.0,
        handlelength=1.5, frameon=True)
    plt.tight_layout()

    # Save the figure
    print plot_data.outf
    savefig(plot_data.outf)


class RateLineData(object):
    def __init__(self, *initial_data, **kwargs):
        for dictionary in initial_data:
            for key in dictionary:
                setattr(self, key, dictionary[key])
        for key in kwargs:
            setattr(self, key, kwargs[key])

class RatePltConfig(object):
    def __init__(self, *initial_data, **kwargs):
        for key in RATE_PLT_CONFIG_DEFAULTS:
            setattr(self, key, RATE_PLT_CONFIG_DEFAULTS[key])
        for dictionary in initial_data:
            for key in dictionary:
                if not hasattr(self, key):
                    print 'WARNING! Unexpected attr: %s' % key
                setattr(self, key, dictionary[key])
        for key in kwargs:
            if not hasattr(self, key):
                print 'WARNING! Unexpected attr: %s' % key
            setattr(self, key, kwargs[key])

        # Parse the lines as well
        if not hasattr(self, 'lines'):
            print 'ERROR! Missing lines!'
            raise ValueError('Missing lines')
        self.lines = [RateLineData(line) for line in self.lines]

def main():
    # Parse arguments
    parser = argparse.ArgumentParser(description='Measure the throughput and '
        'fairness of different frameworks given congestion')
    parser.add_argument('--pltdata', help='Data files containing the lines '
        'and other plot metadata', required=True, type=argparse.FileType('r'))
    args = parser.parse_args()

    user_pltdata = yaml.load(args.pltdata)

    pltdata = RatePltConfig(user_pltdata)

    plot_line_data(pltdata)

    # Show the figure
    show()

if __name__ == '__main__':
    main()
