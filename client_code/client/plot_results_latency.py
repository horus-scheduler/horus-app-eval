"""Usage:
plot_results.py -d <working_dir> [--cdf]

plot_results.py -h | --help
plot_results.py -v | --version

Arguments:
  -d <working_dir> Directory to read results
  
Options:
  -h --help  Displays this message
  -v --version  Displays script version
"""

import numpy.random as nr
import numpy as np
import math
import random 
import matplotlib
#matplotlib.use('Agg')
import matplotlib.pyplot as plt
import pandas as pd
import docopt
import seaborn as sns
import operator
from scipy.stats import kurtosis, skew
import matplotlib.text as mtext

#import seaborn as sns
# Line Styles
DEFAULT_LINE_WIDTH = 8
ALTERNATIVE_LINE_WIDTH = 6
SMALL_LINE_WIDTH = 3
LINE_STYLES = ['--', '-', '-', '-']
FONT_FAMILY = 'Times New Roman'
#FONT_FAMILY = 'Linux Libertine O'

# Font
TEX_ENABLED = False
TICK_FONT_SIZE = 25
AXIS_FONT_SIZE = 27#24
LEGEND_FONT_SIZE = 21
LEGEND_FONT_SIZE_SMALL = 19
CAP_SIZE = LEGEND_FONT_SIZE / 2
AUTLABEL_FONT_SIZE = 20

MARKER_STYLE = dict(markersize=TICK_FONT_SIZE, mew=2.5, mfc='w')
FAILURE_DETECTION_LATENCY=5000000 # 500us

# FONT_DICT = {'family': 'serif', 'serif': 'Times New Roman'}
FONT_DICT = {'family': FONT_FAMILY}

DEFAULT_RC = {'lines.linewidth': DEFAULT_LINE_WIDTH,
              'axes.labelsize': AXIS_FONT_SIZE,
              'xtick.labelsize': TICK_FONT_SIZE,
              'ytick.labelsize': TICK_FONT_SIZE,
              'legend.fontsize': LEGEND_FONT_SIZE,
              'text.usetex': TEX_ENABLED,
              # 'ps.useafm': True,
              # 'ps.use14corefonts': True,
              'font.family': 'sans-serif',
              # 'font.serif': ['Helvetica'],  # use latex default serif font
              }
scale_big = 1.15
DEFAULT_RC_BIG = {'lines.linewidth': int(DEFAULT_LINE_WIDTH * scale_big),
'axes.labelsize': int(AXIS_FONT_SIZE*scale_big),
'xtick.labelsize': int(TICK_FONT_SIZE*scale_big),
'ytick.labelsize': int(TICK_FONT_SIZE*scale_big),
'legend.fontsize': int(LEGEND_FONT_SIZE*scale_big),
'text.usetex': int(TEX_ENABLED*scale_big),
# 'ps.useafm': True,
# 'ps.use14corefonts': True,
'font.family': 'sans-serif',
# 'font.serif': ['Helvetica'],  # use latex default serif font
}

SCATTER_MARKER_DIAMETER = 64

# FONT_DICT = {'family': 'serif', 'serif': 'Times New Roman'}

flatui = ["#0072B2", "#D55E00", "#009E73", "#3498db", "#CC79A7", "#F0E442", "#56B4E9"]
color_pallete = ['#e69d00', '#0071b2', '#009e74', '#cc79a7', '#d54300', '#994F00', '#000000']
markers = ['^', 'o', '*', 's', 'p']

sns.set_context(context='paper', rc=DEFAULT_RC_BIG)
sns.set_style(style='ticks')
plt.rc('text', usetex=TEX_ENABLED)
plt.rc('ps', **{'fonttype': 42})
plt.rc('legend', handlelength=1., handletextpad=0.1)

fig, ax = plt.subplots()

result_dir =  "./"
plot_subdir = "./plots/"
analysis_subdir = "./analysis/"

#TICKS_PER_US = 1000.0
TICKS_PER_US = 1
num_tenants = 20


linestyle = ['--', '--', '--', '--']

class LegendTitle(object):
    def __init__(self, text_props=None):
        self.text_props = text_props or {}
        super(LegendTitle, self).__init__()

    def legend_artist(self, legend, orig_handle, fontsize, handlebox):
        x0, y0 = handlebox.xdescent, handlebox.ydescent
        title = mtext.Text(x0, y0, orig_handle,  **self.text_props)
        handlebox.add_artist(title)
        return title

def plot_proc_latency_cdf(working_dir):
    sns.set_context(context='paper', rc=DEFAULT_RC_BIG)
    plt.rc('font', **FONT_DICT)
    plt.rc('ps', **{'fonttype': 42})
    plt.rc('pdf', **{'fonttype': 42})
    plt.rc('mathtext', **{'default': 'regular'})
    plt.rc('ps', **{'fonttype': 42})
    plt.rc('legend', handlelength=1., handletextpad=0.25)

    fig, ax = plt.subplots()

    path_latency_spine = working_dir + '/spine_latency.csv'
    path_latency_leaf = working_dir + '/leaf_latency.csv'
    latency_spine_raw = np.genfromtxt(path_latency_spine, delimiter=',')
    latency_leaf_raw = np.genfromtxt(path_latency_leaf, delimiter=',')
    
    filter_indices = []
    for idx in range(len(latency_spine_raw)):
        if (latency_spine_raw[idx] < 10000 and latency_spine_raw[idx] > 0) and (latency_leaf_raw[idx] < 10000 and latency_leaf_raw[idx] > 0): 
            # Check overflow (in 32 bit tstamp), only keep valid results
            filter_indices.append(idx)
    latency_spine = latency_spine_raw[filter_indices]
    latency_leaf = latency_leaf_raw[filter_indices]
    # print (len(latency_spine))
    # print (len(latency_leaf))
    latency_tot = [a+b for a, b in zip(latency_spine, latency_leaf)]
    
    latency_spine_sorted = np.sort(latency_spine)
    latency_leaf_sorted = np.sort(latency_leaf)
    latency_tot_sorted = np.sort(latency_tot)
    p_result_spine = 1. * np.arange(len(latency_spine)) / (len(latency_spine) - 1)
    p_result_leaf = 1. * np.arange(len(latency_leaf)) / (len(latency_leaf) - 1)
    p_result_tot = 1. * np.arange(len(latency_tot)) / (len(latency_tot) - 1)

    ax.plot(latency_spine_sorted, p_result_spine*100, linestyle='--',  linewidth=DEFAULT_LINE_WIDTH, color=color_pallete[1], label='Horus Spine')
    ax.plot(latency_leaf_sorted, p_result_leaf*100, linestyle='--',  linewidth=DEFAULT_LINE_WIDTH, color=color_pallete[2], label='Horus Leaf')
    ax.plot(latency_tot_sorted, p_result_tot*100, linestyle='-',  linewidth=DEFAULT_LINE_WIDTH, color=color_pallete[0], label='Total')
    plt.legend(loc='lower right', handlelength=0.7)
    # handles, labels = ax.get_legend_handles_labels()
    # # remove the errorbars
    # handles = [h[0] for h in handles]
    # leg = ax.legend(handles, algorithm_names, ncol=3, handlelength=0.7, borderpad=0.12, borderaxespad=0.0, labelspacing=0.3, columnspacing=0.9, frameon=True, loc='upper center')
    # leg.get_lines()[0].set_linestyle('')
    # bb = leg.get_bbox_to_anchor().inverse_transformed(ax.transAxes)
    # f = leg.get_frame()
    # bb.y0 += 0.12
    # bb.y1 += 0.12
    # leg.set_bbox_to_anchor(bb, transform = ax.transAxes)
    ax.set_ylim(bottom=0, top=100)
    ax.set_ylabel('Fraction of Tasks (%)')
    ax.set_xlabel('Processing Time (ns)')
    ax.spines['right'].set_visible(False)
    ax.spines['top'].set_visible(False)
    plt.grid(True)
    #plt.ticklabel_format(axis="x", style="sci", scilimits=(0,0))
    output_name = 'proc_latency_cdf'
    plt.tight_layout()
    plt.savefig(working_dir + output_name + '.png', ext='png', bbox_inches="tight")
    plt.savefig(working_dir + output_name +'.pdf', ext='pdf', bbox_inches='tight')

def plot_proc_latency_box(working_dir):
    sns.set_context(context='paper', rc=DEFAULT_RC)
    plt.rc('font', **FONT_DICT)
    plt.rc('ps', **{'fonttype': 42})
    plt.rc('pdf', **{'fonttype': 42})
    plt.rc('mathtext', **{'default': 'regular'})
    plt.rc('ps', **{'fonttype': 42})
    plt.rc('legend', handlelength=1., handletextpad=0.25)

    fig, ax = plt.subplots()

    path_latency_spine = working_dir + '/spine_latency.csv'
    path_latency_leaf = working_dir + '/leaf_latency.csv'
    latency_spine_raw = np.genfromtxt(path_latency_spine, delimiter=',')
    latency_leaf_raw = np.genfromtxt(path_latency_leaf, delimiter=',')
    
    filter_indices = []
    for idx in range(len(latency_spine_raw)):
        if (latency_spine_raw[idx] < 10000 and latency_spine_raw[idx] > 0) and (latency_leaf_raw[idx] < 10000 and latency_leaf_raw[idx] > 0): 
            # Check overflow (in 32 bit tstamp), only keep valid results
            filter_indices.append(idx)
    latency_spine = latency_spine_raw[filter_indices]
    latency_leaf = latency_leaf_raw[filter_indices]
    # print (len(latency_spine))
    # print (len(latency_leaf))
    latency_tot = [a+b for a, b in zip(latency_spine, latency_leaf)]
    latency_spine_single = [val for val in latency_spine if val <=600]
    latency_spine_resub = [val for val in latency_spine if val > 600] 
    latency_leaf_single = [val for val in latency_leaf if val <= 600] 
    latency_leaf_resub = [val for val in latency_leaf if val > 600] 

    data_to_plot = [latency_spine, latency_leaf, latency_tot]
    box = ax.boxplot(data_to_plot,
                  positions=[1, 1.6, 2.5],
                  labels=['Spine','Spine Resub','Leaf'])
    plt.show()

if __name__ == "__main__":
    arguments = docopt.docopt(__doc__, version='1.0')
    working_dir = arguments['-d']
    # global result_dir
    result_dir = working_dir
    
    plot_cdf = arguments.get('--cdf')

    percentile_list = list(range(0, 100, 25)) + [99, 99.99]
    
    if plot_cdf:
        plot_proc_latency_cdf(working_dir)
    else:
        plot_proc_latency_box(working_dir)
