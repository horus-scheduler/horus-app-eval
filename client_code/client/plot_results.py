"""Usage:
plot_results.py -d <working_dir> -t <task_distribution> -s <setup> [--overheads]

plot_results.py -h | --help
plot_results.py -v | --version

Arguments:
  -d <working_dir> Directory to save dataset "system_summary.log"
  -t <task_distribution>
  -s <setup>
  
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
import csv

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

# DEFAULT_RC = {'lines.linewidth': DEFAULT_LINE_WIDTH,
#               'axes.labelsize': AXIS_FONT_SIZE,
#               'xtick.labelsize': TICK_FONT_SIZE,
#               'ytick.labelsize': TICK_FONT_SIZE,
#               'legend.fontsize': LEGEND_FONT_SIZE,
#               'text.usetex': TEX_ENABLED,
#               # 'ps.useafm': True,
#               # 'ps.use14corefonts': True,
#               'font.family': 'sans-serif',
#               # 'font.serif': ['Helvetica'],  # use latex default serif font
#               }
scale_big = 1.17
DEFAULT_RC = {'lines.linewidth': int(DEFAULT_LINE_WIDTH * scale_big),
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

sns.set_context(context='paper', rc=DEFAULT_RC)
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

STYLES = {
    'Horus': {
    'color': '#d54300', 
    'marker': '^',
    'linestyle': 'dashed'},

    'RS': {
    'color': "#6666ff",
    'marker': 's',
    'linestyle': 'dashed'},

    'JSQ': {
    'color': "#000000",
    'marker': '*',
    'linestyle': 'dotted'},

    'RS-H': {
    'color': "#0071b2",
    'marker': '8',
    'linestyle': 'dashed'},

    'RS-LB': {
    'color': "#378D37",
    'marker': 'o',
    'linestyle': 'dashed'}
    }

class Experiment:
    def __init__(self, setup, task_dist, plot_overheads=False):
        self.setup = setup
        self.task_dist=task_dist
        self.set_load_points()
        if plot_overheads:
            self.set_overhead_results()
            self.policies = ['saqr', 'rs_rs'] # No additional messages for RS-R
            self.algorithm_names = ['Horus', 'RS-H']
        else:
            #self.policies = ['saqr', 'rs']
            #self.algorithm_names = ['Horus', 'RS']
            self.policies = ['saqr', 'rs_rs', 'rs_rand']
            #self.policies = ['saqr', 'rs_h', 'rs_r']
            self.algorithm_names = ['Horus', 'RS-H', 'RS-LB']

    def set_load_points(self): # The loads (x-axis) in our experiments
        if self.setup == 'b' or self.setup == 'r': # Balanced setup
            if self.task_dist == 'db_port_bimodal': # 50%GET-50%SCAN
                self.loads = [10000, 20000, 30000, 40000, 50000, 60000, 70000, 80000, 90000, 95000, 98000, 100000]
            elif self.task_dist == 'db_bimodal': # 90%GET-10%SCAN
                self.loads = [25000, 50000, 75000, 100000, 125000, 150000, 175000, 200000, 225000, 250000, 275000, 300000, 325000, 350000, 375000]
            elif self.task_dist == 'tpc':
                self.loads = [100000, 200000, 300000, 400000, 450000, 525000, 540000, 555000, 570000]

        elif self.setup == 's': # Skewed setup
            if self.task_dist == 'db_port_bimodal': # 50%GET-50%SCAN
                self.loads = [10000, 20000, 30000, 40000, 50000, 60000, 70000, 80000, 90000, 100000, 110000, 120000, 125000, 130000]
            elif self.task_dist == 'db_bimodal': # 90%GET-10%SCAN
                self.loads = [50000, 100000, 150000, 200000, 250000, 300000, 350000, 400000, 420000, 430000]
            elif self.task_dist == 'tpc':
                self.loads = [100000,200000,300000,400000,450000,525000,600000,650000,675000,690000,700000]

    def set_overhead_results(self): # Experiment results for resub rate and message rate from controller.py (manually compiled here)
        if self.setup == 'b': # Balanced setup
            if self.task_dist == 'db_port_bimodal': # 50%GET-50%SCAN
                self.loads_msg_exp = [10000, 30000, 50000, 70000, 90000, 98000]
                self.task_count = [96829, 286713, 562371, 801549, 895754, 936212]
                self.msg_count_saqr_idle = [5319, 52679, 119421, 128150, 26976, 78]
                self.msg_count_saqr_load = [11437, 29252, 55366, 84172, 108594, 117015]
                self.resub_count_saqr = [1, 2070, 8123, 26382, 105537, 152268]
                self.spine_resub_tot = [5727, 60100, 157732, 282968, 349827, 364625]

            elif self.task_dist == 'db_bimodal': # 90%GET-10%SCAN
                self.loads_msg_exp = [25000, 125000, 225000, 325000, 350000]
                self.task_count = [228964, 1285424, 2215795, 3142268, 5431768]
                self.msg_count_saqr_idle = [6885, 280158, 392126, 76241, 2681]
                self.msg_count_saqr_load = [27758, 125657, 227957, 404771, 678635]
                self.resub_count_saqr = [4, 11627, 99153, 606439, 1076183]
                self.spine_resub_tot = [7200, 336654, 707784, 1054655, 1836757]
            
            elif self.task_dist == 'tpc':
                self.loads_msg_exp = [100000, 200000, 300000, 400000, 525000, 555000]
                self.task_count = [942358, 1945500, 3380757, 4771990, 6183044, 10290208]
                self.msg_count_saqr_idle = [159400, 430782, 726595, 689790, 62710, 726]
                self.msg_count_saqr_load = [97868, 189338, 331769, 510273, 765040, 1286183]
                self.resub_count_saqr = [1481, 24928, 100021, 280126, 1026122, 1783083]
                self.spine_resub_tot = [184218, 528437, 1031560, 1590175, 2341237, 3946639]

        elif self.setup == 's': # Skewed setup
            if self.task_dist == 'db_port_bimodal': # 50%GET-50%SCAN
                self.loads_msg_exp = [20000, 50000, 80000, 110000, 125000]
                self.task_count = [191367, 488177, 783512, 1321588, 2691414]
                self.msg_count_saqr_idle = [0, 382, 58821, 85728, 77864]
                self.msg_count_saqr_load = [5980, 18357, 60722, 110917, 241140]
                self.resub_count_saqr = [0, 169, 32816, 65747, 183204]
                self.spine_resub_tot = [0, 584, 111830, 215936, 386456]

            elif task_dist == 'db_bimodal': # 90%GET-10%SCAN
                self.loads_msg_exp = [50000, 150000, 250000, 350000, 420000]
                self.task_count = [571528, 1992761, 3386639, 4024722, 6391123]
                self.msg_count_saqr_idle = [0, 7169, 268044, 263538, 304641]
                self.msg_count_saqr_load = [17860, 65429, 273016, 340578, 566345]
                self.resub_count_saqr = [0, 1510, 147677, 267969, 607543]
                self.spine_resub_tot = [0, 11478, 423689, 487397, 778036]

            elif self.task_dist == 'tpc':
                self.loads_msg_exp = [100000, 250000, 400000, 550000, 675000]
                self.task_count = [819595, 3118086, 3741140, 6183044, 17837267]
                self.msg_count_saqr_idle = [0, 24473, 309382, 390368, 1110570]
                self.msg_count_saqr_load = [25612, 112195, 330540, 556473, 1573165]
                self.resub_count_saqr = [0, 12061, 195243, 350907, 1250406]
                self.spine_resub_tot = [0, 36993, 503739, 797506, 2594611]

        elif self.setup == 'r': # Rack setup
            if self.task_dist == 'db_port_bimodal': # 50%GET-50%SCAN
                self.loads_msg_exp = [10000, 30000, 50000, 70000, 95000]
                self.task_count = [108045, 322330, 545048, 725700, 1162306]
                self.resub_count_saqr = [0, 0, 0, 77, 115910]

            elif task_dist == 'db_bimodal': # 90%GET-10%SCAN
                self.loads_msg_exp = [25000, 75000, 125000, 175000, 225000, 275000, 325000]
                self.task_count = [238002, 729282, 1345949, 2015484, 2437073, 3187867, 3658582]
                self.resub_count_saqr = [0, 0, 0, 6, 2860, 72412, 476032]

            elif self.task_dist == 'tpc':
                self.loads_msg_exp = [100000, 200000, 300000, 400000, 525000, 555000]
                self.task_count = [100221, 2734616, 3841454, 5605108, 7893740, 8291801]
                self.resub_count_saqr = [0, 161, 336, 18690, 1176689, 1356657]

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

def plot_response_time_from_analysis(experiment, metric, percentile=0.0):
    sns.set_context(context='paper', rc=DEFAULT_RC)
    plt.rc('font', **FONT_DICT)
    plt.rc('ps', **{'fonttype': 42})
    plt.rc('pdf', **{'fonttype': 42})
    plt.rc('mathtext', **{'default': 'regular'})
    plt.rc('ps', **{'fonttype': 42})
    plt.rc('legend', handlelength=1., handletextpad=0.25)
    loads = experiment.loads
    distribution = experiment.task_dist
    policies = experiment.policies
    algorithm_names = experiment.algorithm_names
    use_sci = True
    fig, ax = plt.subplots()
    TICKS_PER_US = 1e3
    x_axis = [int(load/1000) for load in loads] 
    for i, policy in enumerate(policies):
        if percentile == 0: 
            output_tag = '_mean_'
        else:
            output_tag = '_p' + str(percentile) + '_'
        y_axis = []
        y_err = [0] * len(loads)
        max_resp_time = 0
        if metric == 'ratios':
            file_name_response_all = result_dir + policies[i] + '_analysis_ratios.csv'
        else:
            file_name_response_all = result_dir + policies[i] + '_analysis.csv'
        f = open(file_name_response_all)
        csv_reader = list(csv.reader(f))
        if percentile == 0:
            y_axis = [float(x)/TICKS_PER_US for x in csv_reader[3]]
        elif percentile == 50:
            y_axis = [float(x)/TICKS_PER_US for x in csv_reader[2]]
        elif percentile == 99:
            y_axis = [float(x)/TICKS_PER_US for x in csv_reader[1]]
        if len(loads) > len(y_axis): # load was more than capacity for the method append very large value
            y_axis.append(100000)
        print (y_axis)
        print (x_axis)
        print()
        _, caps, bars = plt.errorbar(x_axis, y_axis, linestyle='--', linewidth=ALTERNATIVE_LINE_WIDTH, markersize=15.5, marker=STYLES[algorithm_names[i]]['marker'], color=STYLES[algorithm_names[i]]['color'], label=algorithm_names[i], elinewidth=4, capsize=4, capthick=1)
        [bar.set_alpha(0.3) for bar in bars]
        [cap.set_alpha(0.3) for cap in caps]
    ax.set_xlabel('Load (KRPS)')
    #print(policy)
    # plt.rcParams['legend.handlelength'] = 0.6
    # plt.rcParams['legend.numpoints'] = 1
    handles, labels = ax.get_legend_handles_labels()
    # remove the errorbars
    handles = [h[0] for h in handles]
    #plt.legend(handles, labels, loc='best')
    
    plt.grid(True)

    x_ticks = [100, 300, 500, 700]
    #x_ticks = [200, 400, 550]
    #x_ticks = [20, 40, 60, 80, 100]
    ax.set_xticks(x_ticks)
    # x_ticks = []
    # max_x = 550
    # num_ticks = 5
    # for i in range(num_ticks):
    #     x_ticks.append(max_x)
    #     max_x = int(max_x/5)
    # for x_point in x_axis:
    #     if x_point != 99: 
    #         x_ticks.append(x_point)
    
    #ax.set_yticks([0, 500, 1000, 1500, 2000, 2500])
    if metric != 'qlen' and metric != 'ratios':
        if percentile > 50:
            ax.set_ylim(400, 3000)
        else:
            ax.set_ylim(0, 500)
    else:
        use_sci = False
        if percentile == 0 or percentile <= 50:
            ax.set_ylim(0, 12)
        else:
            ax.set_ylim(0, 40)
    #ax.set_yticks(list(range(0, 4001 , 1000)))
    if metric != 'qlen':
        if percentile == 50:
            text_label = '50% Resp. Time'
        elif percentile == 0:
            text_label = 'Avg. Resp. Time'
        elif percentile == 99:
            text_label = '99% Resp. Time'
    elif metric == 'qlen':
        text_label = 'Worker Queue Len.'
    if metric != 'qlen':
        ax.set_ylabel(text_label + ' (%ss)' % r'$\mu$')
    else:
        ax.set_ylabel(text_label)

    yint=[]
    locs, labels = plt.yticks()
    for each in locs:
        yint.append(int(each))
    plt.yticks(yint)
    ax.spines['right'].set_visible(False)
    ax.spines['top'].set_visible(False)
    
    if use_sci:
        plt.ticklabel_format(axis="y", style="sci", scilimits=(3,3))
    leg = ax.legend(handles, algorithm_names, ncol=3, handlelength=0.7, borderpad=0.1, borderaxespad=0.0, labelspacing=0.2, columnspacing=0.5, frameon=True, loc='upper center')
    for i in range(len(policies)):
        leg.get_lines()[i].set_linestyle('')

    bb = leg.get_bbox_to_anchor().inverse_transformed(ax.transAxes)
    f = leg.get_frame()

    bb.y0 += 0.12
    bb.y1 += 0.12
    leg.set_bbox_to_anchor(bb, transform = ax.transAxes)

    if percentile == 0:
        #'_inc_rsched_'
        output_name =  distribution + '_' +metric + '_avg'
        #plt.title(text_label + ' Mean')
    else:
        output_name = distribution + '_' +metric + '_' + str(percentile)
   
    plt.tight_layout()
    plt.savefig(result_dir + output_name + '.png', ext='png', bbox_inches="tight")
    plt.savefig(result_dir + output_name + '.pdf', ext='pdf', bbox_inches='tight')

def plot_response_time(experiment, metric, percentile=0.0):
    sns.set_context(context='paper', rc=DEFAULT_RC)
    plt.rc('font', **FONT_DICT)
    plt.rc('ps', **{'fonttype': 42})
    plt.rc('pdf', **{'fonttype': 42})
    plt.rc('mathtext', **{'default': 'regular'})
    plt.rc('ps', **{'fonttype': 42})
    plt.rc('legend', handlelength=1., handletextpad=0.25)

    loads = experiment.loads
    distribution = experiment.task_dist
    policies = experiment.policies
    algorithm_names = experiment.algorithm_names

    x_axis = [int(load/1000) for load in loads] 
    
    use_sci = False
    fig, ax = plt.subplots()
    
    if metric != 'qlen':
        TICKS_PER_US = 1000
    else:
        TICKS_PER_US = 1
    #print (cluster_id_list)
    max_y = 0
    for i, policy in enumerate(policies):
        
        if percentile == 0: 
            output_tag = '_mean_'
        else:
            output_tag = '_p' + str(percentile) + '_'
        y_axis = []
        y_err = []
        max_resp_time = 0
        for load in loads:
            try:
                file_name_response_all = 'output_' + policies[i] + '_' +str(distribution) + '_' + str(load)
                overhead = np.genfromtxt(result_dir + file_name_response_all, delimiter='\n')
            except: 
                if load > 200000 and load <=300000: # 100K generated using another machine 
                    file_name_response_all = 'output_' + policies[i] + '_100k_' +str(distribution) + '_' + str(load-100000)     
                elif load > 300000: # 200K generated using another machine 
                    file_name_response_all = 'output_' + policies[i] + '_200k_' +str(distribution) + '_' + str(load-200000)
                elif load <= 200000: # All load generated by one machine
                    file_name_response_all = 'output_' + policies[i] + '_' +str(distribution) + '_' + str(load)

            file_name_response_short = file_name_response_all + '.short'
            file_name_response_long = file_name_response_all + '.long'
            file_name_response_qlen = file_name_response_all + '.queue_lengths'
            try:
                print(result_dir + file_name_response_all)
                if metric == 'all':
                    overhead = np.genfromtxt(result_dir + file_name_response_all, delimiter='\n')
                elif metric == 'short':
                    overhead = np.genfromtxt(result_dir + file_name_response_short, delimiter='\n')
                elif metric == 'long':
                    overhead = np.genfromtxt(result_dir + file_name_response_long, delimiter='\n')
                elif metric == 'qlen':
                    overhead = np.genfromtxt(result_dir + file_name_response_qlen, delimiter='\n')
            # INFO: We ran every experiment till the shceduler no longer stable (queue lens approach infinity)
            # so if a load value has no file replace the result with a very large value
            except: 
                overhead = [100000000]
            if (percentile == 0):
                y_axis.append(np.mean(overhead) / TICKS_PER_US)
                print(metric + " Policy: " + str(algorithm_names[i])+ " load: " + str(load) + " mean: " + str(np.mean(overhead)/TICKS_PER_US))
            else:
                p_val = np.percentile(overhead, percentile) / TICKS_PER_US
                y_axis.append(p_val)
                print(metric + " Policy: " + str(algorithm_names[i])+ " load: " + str(load) + " p" + str(percentile) + ': ' + str(p_val))
            
            # y_err.append(np.std(overhead / TICKS_PER_US))
            y_err.append(0)
            if y_axis[-1] > 1000:
                use_sci = True
            if metric == 'qlen':
                zero_wait_prob = (overhead.shape[0] - np.count_nonzero(overhead, axis=0)) / overhead.shape[0]
                print("Zero wait probbability = " + str(zero_wait_prob))
        _, caps, bars = plt.errorbar(x_axis, y_axis, linestyle='--', linewidth=ALTERNATIVE_LINE_WIDTH, markersize=15.5, marker=STYLES[algorithm_names[i]]['marker'], color=STYLES[algorithm_names[i]]['color'], label=algorithm_names[i], elinewidth=4, capsize=4, capthick=1)
        [bar.set_alpha(0.3) for bar in bars]
        [cap.set_alpha(0.3) for cap in caps]
    ax.set_xlabel('Load (KRPS)')
    #print(policy)
    plt.rcParams['legend.handlelength'] = 0.4
    plt.rcParams['legend.numpoints'] = 1
    handles, labels = ax.get_legend_handles_labels()
    # remove the errorbars
    handles = [h[0] for h in handles]
    leg = ax.legend(handles, algorithm_names, ncol=3, handlelength=0.7, borderpad=0.1, borderaxespad=0.0, labelspacing=0.2, columnspacing=0.5, frameon=True, loc='upper center')
    for i in range(len(policies)):
        leg.get_lines()[i].set_linestyle('')

    bb = leg.get_bbox_to_anchor().inverse_transformed(ax.transAxes)
    f = leg.get_frame()

    bb.y0 += 0.12
    bb.y1 += 0.12
    leg.set_bbox_to_anchor(bb, transform = ax.transAxes)
    plt.grid(True)
    x_ticks = [25, 50, 75, 100, 125]
    # for x_point in x_axis:
    #     if x_point != 99: 
    #         x_ticks.append(x_point)
    ax.set_xticks(x_ticks)
    if metric != 'qlen':
        if percentile > 50:
            ax.set_ylim(0, 7000)
        else:
            ax.set_ylim(0, 3000)
    else:
        use_sci = False
        if percentile == 0 or percentile <= 50:
            ax.set_ylim(0, 8)
        else:
            ax.set_ylim(0, 15)
    ax.set_ylim(0, 7000)
    #ax.set_yticks(list(range(0, 4001 , 1000)))
    if metric != 'qlen':
        if percentile == 50:
            text_label = '50% Resp. Time'
        elif percentile == 0:
            text_label = 'Avg. Resp. Time'
        elif percentile == 99:
            text_label = '99% Resp. Time'
    elif metric == 'qlen':
        text_label = 'Worker Queue Len.'
    if metric != 'qlen':
        ax.set_ylabel(text_label + ' (%ss)' % r'$\mu$')
    else:
        ax.set_ylabel(text_label)

    yint=[]
    locs, labels = plt.yticks()
    for each in locs:
        yint.append(int(each))
    plt.yticks(yint)
    ax.spines['right'].set_visible(False)
    ax.spines['top'].set_visible(False)
    
    if use_sci:
        plt.ticklabel_format(axis="y", style="sci", scilimits=(3,3))
        

    if percentile == 0:
        #'_inc_rsched_'
        output_name =  distribution + '_' +metric + '_avg'
        #plt.title(text_label + ' Mean')
    else:
        output_name = distribution + '_' +metric + '_' + str(percentile)
   
    plt.tight_layout()
    plt.savefig(result_dir + plot_subdir + output_name + '.png', ext='png', bbox_inches="tight")
    plt.savefig(result_dir + plot_subdir + output_name + '.pdf', ext='pdf', bbox_inches='tight')
    #plt.show(fig)

def plot_msg_rate(experiment, logarithmic=True):
    sns.set_context(context='paper', rc=DEFAULT_RC)
    plt.rc('font', **FONT_DICT)
    plt.rc('ps', **{'fonttype': 42})
    plt.rc('pdf', **{'fonttype': 42})
    plt.rc('mathtext', **{'default': 'regular'})
    plt.rc('ps', **{'fonttype': 42})
    plt.rc('legend', handlelength=1., handletextpad=0.25)
    
    policies = experiment.policies
    distribution = experiment.task_dist
    algorithm_names = experiment.algorithm_names
    loads_msg_exp = experiment.loads_msg_exp

    x_axis = [load/1000 for load in loads_msg_exp]
    
    fig, ax = plt.subplots()
    
    for i, policy in enumerate(policies):
        y_axis = []
        for load_idx, load in enumerate(loads_msg_exp):
            exp_duration_s = float(experiment.task_count[load_idx])/load # Duration of experiment in seconds
            if policy == 'saqr':
                total_msgs = experiment.msg_count_saqr_load[load_idx] + experiment.msg_count_saqr_idle[load_idx]
            else:
                total_msgs = experiment.msg_count_rs[load_idx]
            y_axis.append(total_msgs/exp_duration_s)
        #print np.mean(y_axis)
        print("Msg Rate of " + str(policy))
        print(y_axis)
        plt.plot(x_axis, y_axis, '--', linewidth=ALTERNATIVE_LINE_WIDTH, markersize=20, marker=STYLES[algorithm_names[i]]['marker'], color=STYLES[algorithm_names[i]]['color'], label=algorithm_names[i])
    
    ax.set_xlabel('Load (KRPS)')
    ax.set_ylabel('#Msgs/s')
    
    #ax.get_yaxis().get_offset_text().set_visible(False)
    sns.despine(ax=ax, top=True, right=True, left=False, bottom=False)
    if logarithmic:
        ax.set_yscale('log')
    ax.set_ylim(0, 10e5)
    #ax.set_xticks(x_axis)
    
    plt.rcParams['legend.handlelength'] = 0.2
    plt.rcParams['legend.numpoints'] = 1
    handles, labels = ax.get_legend_handles_labels()
    # remove the errorbars
    #handles = [h[0] for h in handles]
    plt.legend(handles, labels, loc='best', handletextpad=0.5)
    
    plt.grid(True)
    
    output_name = distribution + '_' + experiment.setup +'_msg_per_sec'
    
    #plt.title('Msg Rate ' + stat_tag)
    plt.tight_layout()
    plt.savefig(result_dir + plot_subdir + output_name + '.png', ext='png', bbox_inches="tight")
    plt.savefig(result_dir + plot_subdir + output_name + '.pdf', ext='pdf', bbox_inches='tight')

def plot_resub_rate(experiment):
    
    sns.set_context(context='paper', rc=DEFAULT_RC)
    plt.rc('font', **FONT_DICT)
    plt.rc('ps', **{'fonttype': 42})
    plt.rc('pdf', **{'fonttype': 42})
    plt.rc('mathtext', **{'default': 'regular'})
    plt.rc('ps', **{'fonttype': 42})
    plt.rc('legend', handlelength=1., handletextpad=0.25)
    
    policies = experiment.policies
    distribution = experiment.task_dist
    algorithm_names = experiment.algorithm_names
    loads_msg_exp = experiment.loads_msg_exp

    x_axis = [load/1000 for load in loads_msg_exp]
    ind = np.arange(len(x_axis))
    offset = 0.3

    fig, ax = plt.subplots()


    y_axis = []
    y_axis_spine_resub = []
    for load_idx, load in enumerate(loads_msg_exp):
        #exp_duration_s = float(task_count[load_idx])/load # Duration of experiment in seconds
        print(load)
        print(experiment.resub_count_saqr[load_idx])
        print (experiment.task_count[load_idx])
        resub_count_spine = (experiment.spine_resub_tot[load_idx] - (experiment.msg_count_saqr_idle[load_idx]/2))
        y_axis_spine_resub.append((resub_count_spine/experiment.task_count[load_idx]) * 100)
        y_axis.append((experiment.resub_count_saqr[load_idx]/experiment.task_count[load_idx]) * 100)
    
    print("Resubmit Rate:")
    print(y_axis)
    plt.bar(ind, y_axis,  width=offset, color=color_pallete[1], label='Horus Leaf');
    plt.bar(ind+ offset, y_axis_spine_resub,  width=offset, color=color_pallete[2], label='Horus Spine');
    print (np.mean(y_axis))
    ax.set_xlabel('Load (KRPS)')
    ax.set_ylabel('Fraction of Tasks (%)', fontsize=AXIS_FONT_SIZE)
    
    ax.get_yaxis().get_offset_text().set_visible(False)
    sns.despine(ax=ax, top=True, right=True, left=False, bottom=False)
    
    plt.xticks(ind + offset/2, [str(int(x)) for x in x_axis])
    
    plt.rcParams['legend.handlelength'] = 1
    plt.rcParams['legend.numpoints'] = 1
    handles, labels = ax.get_legend_handles_labels()
    plt.legend(handles, labels, loc='best')
    plt.grid(True)
    
    output_name = distribution + '_' + experiment.setup + '_resub_rate'
    
    #plt.title('Msg Rate ' + stat_tag)
    plt.tight_layout()
    plt.savefig(result_dir + plot_subdir + output_name + '.png', ext='png', bbox_inches="tight")
    plt.savefig(result_dir + plot_subdir + output_name + '.pdf', ext='pdf', bbox_inches='tight')


def plot_comm_overherad_normalized(experiment, stacked=True):
    sns.set_context(context='paper', rc=DEFAULT_RC)
    plt.rc('font', **FONT_DICT)
    plt.rc('ps', **{'fonttype': 42})
    plt.rc('pdf', **{'fonttype': 42})
    plt.rc('mathtext', **{'default': 'regular'})
    plt.rc('ps', **{'fonttype': 42})
    plt.rc('legend', handlelength=1., handletextpad=0.25)
    
    policies = experiment.policies
    distribution = experiment.task_dist
    algorithm_names = experiment.algorithm_names
    loads_msg_exp = experiment.loads_msg_exp

    x_axis = [load/1000 for load in loads_msg_exp]
    ind = np.arange(len(x_axis))
    fig, ax = plt.subplots()
    policy = 'saqr'
    if stacked:
        width=0.4
        offset = 0
    else:
        width=0.2
        offset = width
    
    y_axis_task_resub = []
    y_axis_state_update = []
    y_axis_spine_resub = []
    y_axis_rs_overhead=[]
    y_axis_state_update_idle = []

    y_axis_state_update_load = []
    for load_idx, load in enumerate(loads_msg_exp):
        exp_duration_s = experiment.task_count[load_idx]

        y_axis_task_resub.append((experiment.resub_count_saqr[load_idx]/exp_duration_s))
        y_axis_state_update_load.append(experiment.msg_count_saqr_load[load_idx]/exp_duration_s)
        y_axis_state_update_idle.append(experiment.msg_count_saqr_idle[load_idx]/exp_duration_s)
        y_axis_spine_resub.append(experiment.spine_resub_tot[load_idx]/exp_duration_s)
    
    y_axis_spine_resub = np.array(y_axis_spine_resub)
    y_axis_state_update_load = np.array(y_axis_state_update_load)
    y_axis_state_update_idle = np.array(y_axis_state_update_idle)

    #plt.bar(ind, y_axis_task_resub,  width=width, color=color_pallete[0], label='Leaf Resubmitted Pkts')
    
    plt.bar(ind, y_axis_state_update_load, bottom=y_axis_state_update_idle, width=width, color=color_pallete[0], label='Load Updates')
    plt.bar(ind + 1*offset, y_axis_state_update_idle, bottom=0 if stacked else 0, width=width, color=color_pallete[1],  label='Idle Add/Remove')
    # plt.bar(ind + 2*offset, y_axis_spine_resub, width=width, color=color_pallete[3], label='Resubmitted Pkts')
    print("Saqr:")
    #print([y_axis_resub_tot + y_axis_state_update for y_axis_resub_tot, y_axis_state_update in zip(y_axis_resub_tot, y_axis_state_update)])
    ax.set_xlabel('Load (KRPS)')
    ax.set_ylabel('#Msgs/task')
    
    ax.spines['right'].set_visible(False)
    ax.spines['top'].set_visible(False)

    #sns.despine(ax=ax, top=True, right=True, left=False, bottom=False)
    #plt.ticklabel_format(axis="y", style="sci", scilimits=(0,0))

    #ax.set_xticks([str(int(x)) for x in x_axis])
    
    plt.xticks(ind + offset, [str(int(x)) for x in x_axis])
    
    # plt.rcParams['legend.handlelength'] = 1
    # plt.rcParams['legend.numpoints'] = 1
    handles, labels = ax.get_legend_handles_labels()
    # handles.insert(0, 'Horus')
    # labels.insert(0, '')

    # handles.insert(3, 'RS-H')
    # labels.insert(3, '')

    ax.legend(handles, labels, fontsize=LEGEND_FONT_SIZE_SMALL, handler_map={str: LegendTitle({'fontsize': LEGEND_FONT_SIZE})})
    # plt.legend(handles, labels, loc='best')
    plt.grid(True)
    
    output_name = distribution + '_' + experiment.setup + '_comm_overhead'
    
    #plt.title('Msg Rate ' + stat_tag)
    plt.tight_layout()
    plt.savefig(result_dir + plot_subdir + output_name + '.png', ext='png', bbox_inches="tight")
    plt.savefig(result_dir + plot_subdir + output_name + '.pdf', ext='pdf', bbox_inches='tight')



def plot_proc_overherad(experiment, normalized=True):
    sns.set_context(context='paper', rc=DEFAULT_RC)
    plt.rc('font', **FONT_DICT)
    plt.rc('ps', **{'fonttype': 42})
    plt.rc('pdf', **{'fonttype': 42})
    plt.rc('mathtext', **{'default': 'regular'})
    plt.rc('ps', **{'fonttype': 42})
    plt.rc('legend', handlelength=1., handletextpad=0.25)
    
    policies = experiment.policies
    distribution = experiment.task_dist
    algorithm_names = experiment.algorithm_names
    loads_msg_exp = experiment.loads_msg_exp

    x_axis = [load/1000 for load in loads_msg_exp]
    ind = np.arange(len(x_axis))
    fig, ax = plt.subplots()
    policy = 'saqr'
    width=0.4
    y_axis_task_resub = []
    y_axis_state_update = []
    y_axis_spine_resub = []
    y_axis_rs_overhead=[]
    for load_idx, load in enumerate(loads_msg_exp):
        if normalized:
            exp_duration_s = experiment.task_count[load_idx]
        else:
            exp_duration_s = float(experiment.task_count[load_idx])/load # Duration of experiment in seconds
        total_resub = experiment.resub_count_saqr[load_idx]
        if policy == 'saqr':
            total_msgs = experiment.msg_count_saqr_load[load_idx] + experiment.msg_count_saqr_idle[load_idx]
        y_axis_task_resub.append((total_resub/exp_duration_s))
        y_axis_state_update.append(total_msgs/exp_duration_s)
        y_axis_spine_resub.append(experiment.spine_resub_tot[load_idx]/exp_duration_s)
        y_axis_resub_tot = [y_axis_task_resub + y_axis_spine_resub for y_axis_task_resub, y_axis_spine_resub in zip(y_axis_task_resub, y_axis_spine_resub)]
        y_axis_rs_overhead.append(experiment.msg_count_rs[load_idx]/exp_duration_s)
    if not normalized:
        y_axis_rs_overhead = [y * 2 for y in y_axis_rs_overhead] # Once processed by leaf and another time processed by spine
        y_axis_state_update = [y * 2 for y in y_axis_state_update] # Once processed by leaf and another time processed by spine
    
    
    plt.bar(ind, y_axis_resub_tot,  width=width, color=STYLES['Horus']['color'], hatch='///',  label='Resubmitted Pkts')
    plt.bar(ind, y_axis_state_update, bottom=y_axis_resub_tot,  width=width, color=STYLES['Horus']['color'], hatch="++", label='Update Msgs')
    if not normalized:
        plt.bar(ind + width, y_axis_rs_overhead, width=width, color=STYLES['RS-H']['color'], label='Update Msgs')
        print("RS:")
        print(y_axis_rs_overhead)
    print("Saqr:")
    print([y_axis_resub_tot + y_axis_state_update for y_axis_resub_tot, y_axis_state_update in zip(y_axis_resub_tot, y_axis_state_update)])
    ax.set_xlabel('Load (KRPS)')
    ax.set_ylabel('#Pkts/s')
    
    ax.spines['right'].set_visible(False)
    ax.spines['top'].set_visible(False)

    #sns.despine(ax=ax, top=True, right=True, left=False, bottom=False)
    plt.ticklabel_format(axis="y", style="sci", scilimits=(0,0))

    #ax.set_xticks([str(int(x)) for x in x_axis])
    plt.xticks(ind + width / 2, [str(int(x)) for x in x_axis])
    
    # plt.rcParams['legend.handlelength'] = 1
    # plt.rcParams['legend.numpoints'] = 1
    handles, labels = ax.get_legend_handles_labels()
    handles.insert(0, 'Horus')
    labels.insert(0, '')

    handles.insert(3, 'RS-H')
    labels.insert(3, '')

    ax.legend(handles, labels, fontsize=LEGEND_FONT_SIZE_SMALL,
        handler_map={str: LegendTitle({'fontsize': LEGEND_FONT_SIZE})})
    # plt.legend(handles, labels, loc='best')
    plt.grid(True)
    
    output_name = distribution + '_' + experiment.setup + '_proc_overhead'
    
    #plt.title('Msg Rate ' + stat_tag)
    plt.tight_layout()
    plt.savefig(result_dir + plot_subdir + output_name + '.png', ext='png', bbox_inches="tight")
    plt.savefig(result_dir + plot_subdir + output_name + '.pdf', ext='pdf', bbox_inches='tight')

if __name__ == "__main__":
    arguments = docopt.docopt(__doc__, version='1.0')
    working_dir = arguments['-d']
    # global result_dir
    result_dir = working_dir
    task_dist = arguments['-t']
    setup = arguments['-s']
    plot_overheads = arguments.get('--overheads')
    
    experiment = Experiment(setup, task_dist, plot_overheads)

    percentile_list = list(range(0, 100, 25)) + [99, 99.99]
    
    if plot_overheads:
        #plot_msg_rate(experiment, True)
        plot_resub_rate(experiment)
        plot_comm_overherad_normalized(experiment)
    else:
        plot_response_time(experiment, 'all', percentile=99)
        #plot_response_time(experiment, 'all', percentile=50)
        #plot_response_time(experiment, 'all', percentile=0)
        #plot_response_time(experiment, 'all', percentile=99)
        # plot_response_time(experiment, 'all', task_dist, percentile=50)
        # plot_response_time(experiment, 'all', task_dist, percentile=0)
        # plot_response_time(experiment, 'qlen', task_dist, percentile=99)
        # plot_response_time(experiment, 'qlen', task_dist, percentile=0)
        #plot_response_time(experiment, 'short', task_dist, percentile=99)
        #plot_response_time(experiment, 'short', task_dist, percentile=50)
        #plot_response_time(experiment, 'short', task_dist, percentile=0)
        