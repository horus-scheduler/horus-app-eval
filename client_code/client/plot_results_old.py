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
            #self.policies = ['saqr', 'rs_rs', 'rs_rand']
            self.policies = ['saqr', 'rs_h', 'rs_r']
            self.algorithm_names = ['Horus', 'RS-H', 'RS-R']

    def set_load_points(self): # The loads (x-axis) in our experiments
        if self.setup == 'b': # Balanced setup
            if self.task_dist == 'db_port_bimodal': # 50%GET-50%SCAN
                self.loads = [10000, 20000, 30000, 40000, 50000, 60000, 70000, 80000, 90000, 95000, 98000, 100000]
            elif self.task_dist == 'db_bimodal': # 90%GET-10%SCAN
                self.loads = [25000, 50000, 75000, 100000, 125000, 150000, 175000, 200000, 225000, 250000, 275000, 300000, 325000, 350000]
            elif self.task_dist == 'tpc':
                self.loads = [100000, 200000, 300000, 400000, 450000, 525000, 540000, 555000, 570000]

        elif self.setup == 's': # Skewed setup
            if self.task_dist == 'db_port_bimodal': # 50%GET-50%SCAN
                self.loads = [10000, 20000, 30000, 40000, 50000, 60000, 70000, 80000, 90000, 100000, 110000, 115000, 120000]
            elif self.task_dist == 'db_bimodal': # 90%GET-10%SCAN
                self.loads = [50000, 100000, 150000, 200000, 250000, 300000, 350000, 400000, 425000, 450000]
            elif self.task_dist == 'tpc':
                self.loads = [100000, 200000, 300000, 400000, 450000, 525000, 570000, 600000, 625000, 650000, 660000, 675000, 700000]

    def set_overhead_results(self): # Experiment results for resub rate and message rate from controller.py (manually compiled here)
        if self.setup == 'b': # Balanced setup
            if self.task_dist == 'db_port_bimodal': # 50%GET-50%SCAN
                self.loads_msg_exp = [10000, 30000, 50000, 70000, 90000]
                self.task_count = [102560, 292162, 578239, 890608, 1798504, 1316483]
                self.msg_count_rs = [102560, 292162, 578239, 890608, 1798504, 1316483]
                self.msg_count_saqr_idle = [7568, 109750, 236612, 357465, 230162, 41352]
                self.msg_count_saqr_load = [11946, 22799, 42700, 66640, 196039, 159389]
                self.resub_count_saqr = [1, 3759, 19145, 48413, 201796, 186353]
                self.spine_resub_tot = [3788, 55254, 120855, 203899, 487206]

            elif self.task_dist == 'db_bimodal': # 90%GET-10%SCAN
                self.loads_msg_exp = [25000, 75000, 125000, 175000, 225000, 275000, 325000]
                self.task_count = [437475, 1108641, 1565499, 1974106, 2976202, 4084485, 4470051]
                self.msg_count_rs = [437475, 1108641, 1565499, 1974106, 2976202, 4084485, 4470051]
                self.msg_count_saqr_idle = [14975, 335435, 532984, 715349, 1078293, 1231909, 439239]
                self.msg_count_saqr_load = [52809, 96648, 129061, 157343, 237235, 356568,  503847]
                self.resub_count_saqr = [8, 17286, 55010, 109472, 214377, 414855, 636794]
                self.spine_resub_tot = [7502, 169936, 274184, 375707, 600731, 889632, 1089833]
            
            elif self.task_dist == 'tpc':
                self.loads_msg_exp = [100000, 200000, 300000, 400000, 525000, 555000]
                self.task_count = [1295326, 2397387, 3518037, 5466545, 5721817, 8673196]
                self.msg_count_rs = [1295326, 2397387, 3518037, 5466545, 5721817, 8673196]
                self.msg_count_saqr_idle = [282484, 634630, 981760, 1435053, 245547, 38650]
                self.msg_count_saqr_load = [86078, 145393, 207032, 333006, 505831, 808421]
                self.resub_count_saqr = [6222, 53930, 137086, 319330, 614850, 906999]
                self.spine_resub_tot = [191755, 443214, 709304, 1192716, 1276503, 1985780]

        elif self.setup == 's': # Skewed setup
            if self.task_dist == 'db_port_bimodal': # 50%GET-50%SCAN
                self.loads_msg_exp = [10000, 30000, 50000, 70000, 90000, 110000, 115000]
                self.task_count = [118992, 414376, 520829, 703844, 1349961, 1541800, 2888710]
                self.msg_count_rs = [118992, 414376, 520829, 703844, 1349961, 1541800, 2888710]
                self.msg_count_saqr_idle = [0, 14, 110788, 144265, 319230, 303249, 311433]
                self.msg_count_saqr_load = [3718, 12948, 37866, 50736, 94928, 132547, 276135]
                self.resub_count_saqr = [0, 0, 26278, 41564, 105095, 135135, 333247]
                self.spine_resub_tot = [0, 7, 56924, 75420, 195051, 373372, 529106]

            elif task_dist == 'db_bimodal': # 90%GET-10%SCAN
                self.loads_msg_exp = [50000, 150000, 250000, 350000, 425000]
                self.task_count = [698139, 1704212, 2905707, 7187813, 4965224]
                self.msg_count_rs = [698139, 1704212, 2905707, 7187813, 4965224]
                self.msg_count_saqr_idle = [0, 310429,  534967, 1783106, 485409]
                self.msg_count_saqr_load = [22168, 125340, 225171, 605849, 471860]
                self.resub_count_saqr = [0, 85193, 202520, 636317, 644160]
                self.spine_resub_tot = [0, 164558, 340858, 1433620, 744684]

            elif self.task_dist == 'tpc':
                self.loads_msg_exp = [100000, 200000, 300000, 400000, 525000, 600000, 660000]
                self.task_count = [1334395, 2814127, 4204907, 4650662, 7996040, 10228321,  11941479]
                self.msg_count_rs = [1334395, 2814127, 4204907, 4650662, 7996040, 10228321,  11941479]
                self.msg_count_saqr_idle = [0, 137247, 803825, 704394, 828325, 564597,  67285]
                self.msg_count_saqr_load = [20310, 43392, 297948, 305743, 490904, 706540,  940950]
                self.resub_count_saqr = [0, 32298, 289942, 341530, 492203, 590434,  791931]
                self.spine_resub_tot = [0, 76920, 475411, 476239, 1201687, 2111251,  1752424]

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
            y_axis.append(100000000)
        print (y_axis)
        print (x_axis)
        print(y_err)
        
        _, caps, bars = plt.errorbar(x_axis, y_axis, linestyle='--', yerr=y_err, linewidth=ALTERNATIVE_LINE_WIDTH, markersize=15.5, marker=markers[i], color=color_pallete[i], label=algorithm_names[i], elinewidth=4, capsize=4, capthick=1)
        [bar.set_alpha(0.3) for bar in bars]
        [cap.set_alpha(0.3) for cap in caps]
    ax.set_xlabel('Load (KRPS)')
    #print(policy)
    plt.rcParams['legend.handlelength'] = 0.3
    plt.rcParams['legend.numpoints'] = 1
    handles, labels = ax.get_legend_handles_labels()
    # remove the errorbars
    handles = [h[0] for h in handles]
    plt.legend(handles, labels, loc='best')
    
    plt.grid(True)
    x_ticks = [100, 250, 400, 550, 700]
    # max_x = 550000
    # num_ticks = 5
    # for i in range(num_ticks):
    #     x_ticks.append(max_x)
    #     max_x = int(max_x/5)
    # for x_point in x_axis:
    #     if x_point != 99: 
    #         x_ticks.append(x_point)
    ax.set_xticks(x_ticks)
    if metric != 'qlen' and metric != 'ratios':
        if percentile > 50:
            ax.set_ylim(0, 2500)
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
            text_label = '50% Response Time'
        elif percentile == 0:
            text_label = 'Avg. Response Time'
        elif percentile == 99:
            text_label = '99% Response Time'
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
        _, caps, bars = plt.errorbar(x_axis, y_axis, linestyle='--', yerr=y_err, linewidth=ALTERNATIVE_LINE_WIDTH, markersize=15.5, marker=markers[i], color=color_pallete[i], label=algorithm_names[i], elinewidth=4, capsize=4, capthick=1)
        [bar.set_alpha(0.3) for bar in bars]
        [cap.set_alpha(0.3) for cap in caps]
    ax.set_xlabel('Load (KRPS)')
    #print(policy)
    plt.rcParams['legend.handlelength'] = 0
    plt.rcParams['legend.numpoints'] = 1
    handles, labels = ax.get_legend_handles_labels()
    # remove the errorbars
    handles = [h[0] for h in handles]
    plt.legend(handles, labels, loc='best')
    
    plt.grid(True)
    # x_ticks = []
    # for x_point in x_axis:
    #     if x_point != 99: 
    #         x_ticks.append(x_point)
    # ax.set_xticks(x_ticks)
    if metric != 'qlen':
        if percentile > 50:
            ax.set_ylim(0, 8000)
        else:
            ax.set_ylim(0, 3000)
    else:
        use_sci = False
        if percentile == 0 or percentile <= 50:
            ax.set_ylim(0, 8)
        else:
            ax.set_ylim(0, 15)
    #ax.set_yticks(list(range(0, 4001 , 1000)))
    if metric != 'qlen':
        if percentile == 50:
            text_label = '50% Response Time'
        elif percentile == 0:
            text_label = 'Avg. Response Time'
        elif percentile == 99:
            text_label = '99% Response Time'
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
        plt.plot(x_axis, y_axis, '--', linewidth=ALTERNATIVE_LINE_WIDTH, markersize=20, marker=markers[i], color=color_pallete[i], label=algorithm_names[i])
    
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
    
    fig, ax = plt.subplots()


    y_axis = []
    for load_idx, load in enumerate(loads_msg_exp):
        #exp_duration_s = float(task_count[load_idx])/load # Duration of experiment in seconds
        total_resub = experiment.resub_count_saqr[load_idx]    
        y_axis.append((total_resub/experiment.task_count[load_idx])*100)
    
    print("Resubmit Rate:")
    print(y_axis)
    plt.bar([str(int(x)) for x in x_axis], y_axis,  width=0.5, color=color_pallete[0])
    print (np.mean(y_axis))
    ax.set_xlabel('Load (KRPS)')
    ax.set_ylabel('Fraction of Tasks (%)', fontsize=LEGEND_FONT_SIZE_SMALL)
    
    ax.get_yaxis().get_offset_text().set_visible(False)
    sns.despine(ax=ax, top=True, right=True, left=False, bottom=False)
    
    ax.set_xticks([str(int(x)) for x in x_axis])
    
    plt.rcParams['legend.handlelength'] = 1
    plt.rcParams['legend.numpoints'] = 1
    handles, labels = ax.get_legend_handles_labels()
    #plt.legend(handles, labels, loc='best')
    plt.grid(True)
    
    output_name = distribution + '_' + experiment.setup + '_resub_rate'
    
    #plt.title('Msg Rate ' + stat_tag)
    plt.tight_layout()
    plt.savefig(result_dir + plot_subdir + output_name + '.png', ext='png', bbox_inches="tight")
    plt.savefig(result_dir + plot_subdir + output_name + '.pdf', ext='pdf', bbox_inches='tight')


def plot_proc_overherad(experiment):
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
        exp_duration_s = float(experiment.task_count[load_idx])/load # Duration of experiment in seconds
        total_resub = experiment.resub_count_saqr[load_idx]
        if policy == 'saqr':
            total_msgs = experiment.msg_count_saqr_load[load_idx] + experiment.msg_count_saqr_idle[load_idx]
        y_axis_task_resub.append((total_resub/exp_duration_s))
        y_axis_state_update.append(total_msgs/exp_duration_s)
        y_axis_spine_resub.append(experiment.spine_resub_tot[load_idx]/exp_duration_s)
        y_axis_resub_tot = [y_axis_task_resub + y_axis_spine_resub for y_axis_task_resub, y_axis_spine_resub in zip(y_axis_task_resub, y_axis_spine_resub)]
        y_axis_rs_overhead.append(experiment.msg_count_rs[load_idx]/exp_duration_s)
    
    y_axis_rs_overhead = [y * 2 for y in y_axis_rs_overhead] # Once processed by leaf and another time processed by spine
    y_axis_state_update = [y * 2 for y in y_axis_state_update] # Once processed by leaf and another time processed by spine
    
    
    plt.bar(ind, y_axis_resub_tot,  width=width, color=color_pallete[0], hatch='///',  label='Resubmitted Pkts')
    plt.bar(ind, y_axis_state_update, bottom=y_axis_resub_tot,  width=width, color=color_pallete[0], hatch="++", label='Update Msgs')
    plt.bar(ind + width, y_axis_rs_overhead, width=width, color=color_pallete[1], label='Update Msgs')
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
        plot_msg_rate(experiment, True)
        plot_resub_rate(experiment)
        plot_proc_overherad(experiment)
    else:
        plot_response_time_from_analysis(experiment, 'all', percentile=99)
        plot_response_time_from_analysis(experiment, 'all', percentile=50)
        plot_response_time_from_analysis(experiment, 'all', percentile=0)
        #plot_response_time(experiment, 'all', percentile=99)
        # plot_response_time(experiment, 'all', task_dist, percentile=50)
        # plot_response_time(experiment, 'all', task_dist, percentile=0)
        # plot_response_time(experiment, 'qlen', task_dist, percentile=99)
        # plot_response_time(experiment, 'qlen', task_dist, percentile=0)
        #plot_response_time(experiment, 'short', task_dist, percentile=99)
        #plot_response_time(experiment, 'short', task_dist, percentile=50)
        #plot_response_time(experiment, 'short', task_dist, percentile=0)
