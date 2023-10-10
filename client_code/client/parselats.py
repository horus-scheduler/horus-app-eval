#!/usr/bin/python3

import sys
import os
import numpy as np
import math
import random 
import matplotlib
import seaborn as sns
#matplotlib.use('Agg')
import matplotlib.pyplot as plt
#from scipy import stats

#import seaborn as sns
# Line Styles
DEFAULT_LINE_WIDTH = 8
ALTERNATIVE_LINE_WIDTH = 4
SMALL_LINE_WIDTH = 2
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

# Configurations for each dynamic scneario uncomment below
is_wide = True
# 1. Static rate, leaf failure and recovery
event_s = [5, 15]
event_desc = ['leaf switch failure', 'add workers from another rack']
tick_interval = 5
skip = 0

# 2. Dynamic rate and add resource (_drate_add_resource_xxx) 
# event_s = [10, 22, 31]
# event_desc = ['add server', 'add client', 'add server']
# tick_interval = 10
# skip = 8

# 3. Dynamic rate with 10s intervals (_drate_10000_*) 
# event_s = [10, 20, 30, 40 , 50, 60]
# event_desc = ['+ client', '+ client', '+ client', '- client', '- client', '- client']
# tick_interval = 10
# skip = 10

# 4. Dynamic rate with 5s intervals (_drate_5000_xxx)
# event_s = [5, 10, 15, 20, 25 , 30]
# event_desc = ['+ client', '+ client', '+ client', '- client', '- client', '- client']
# tick_interval = 5
# skip = 4

if __name__ == '__main__':
    def getLatPct(latsFile):
        assert os.path.exists(latsFile)
        sns.set_context(context='paper', rc=DEFAULT_RC)
        plt.rc('font', **FONT_DICT)
        plt.rc('ps', **{'fonttype': 42})
        plt.rc('pdf', **{'fonttype': 42})
        plt.rc('mathtext', **{'default': 'regular'})
        plt.rc('ps', **{'fonttype': 42})
        plt.rc('legend', handlelength=1., handletextpad=0.25)
        if is_wide:
            fig, ax = plt.subplots(figsize=(9, 4.8))
        else:
            fig, ax = plt.subplots()


        use_sci = True
        result = values = np.genfromtxt(latsFile, delimiter='\n')
        begins = []
        ends = []
        begin = 0
        # Find intervals of 1s based on the task generation time 
        try:
            time_us = np.genfromtxt(latsFile + ".gen_us", delimiter='\n')
            
            for i, send_time in enumerate(time_us):
                if send_time - time_us[begin] >= 1e6:
                    begins.append(begin)
                    ends.append(i)
                    begin = i
        except: 
            # In case of static rate (e.g., failure scenario) just use the last value after '_',
            # as rate and find seconds based on that: e.g., rate is 65000 -> every 65000 row is one second
            sp = latsFile.split('_')
            rate = int(sp[len(sp)-1])
            end = rate
            while 1:
                if begin > len(result):
                    break
                begins.append(begin)
                ends.append(end)
                end += rate
                begin += rate
            
        print (begins)
        print (ends)
        
        sjrnTimes = [l/1e3 for l in result]
        
        #end = rate
        x_axis = []
        y_axis = []
        y_axis_avg = []
        sec = 0
        r_idx = 0
        
        for i in range(skip,len(begins)):
            if ends[i] >= len(result):
                break
            y_axis.append(np.percentile(sjrnTimes[begins[i]:ends[i]], 99))
            y_axis_avg.append(np.mean(sjrnTimes[begins[i]:ends[i]]))
            sec +=1
        print (y_axis)
        #return
        #x_ticks = [0]
        avg = np.mean(np.array(y_axis))
        # for i, val in enumerate(y_axis):
        #     if val > avg:
        #         x_ticks.append(i)
        #         break
        # x_ticks.append(x_ticks[1]+5)
        # x_ticks.append(x_ticks[1]+10)
        # x_ticks.append(x_ticks[1]+15)
        # x_ticks.append(x_ticks[1]+20)
        # x_ticks.append(x_ticks[1]+25)
        x_ticks = range(0 , sec, tick_interval)
        if use_sci:
            plt.ticklabel_format(axis="y", style="sci", scilimits=(3,3))
        plt.plot(range(0, sec), y_axis, linestyle='-', label='99%', color='#d54300', linewidth=ALTERNATIVE_LINE_WIDTH)
        plt.plot(range(0, sec), y_axis_avg, linestyle='-', label='Avg.', color='#009e74', linewidth=ALTERNATIVE_LINE_WIDTH)

        for i, second in enumerate(event_s):
            #second -= skip
            ax.annotate(event_desc[i],
                xy=(second-1, y_axis[second-1]), 
                xytext=(second - 2, y_axis[second-1]-0.25*np.max(y_axis)),
                horizontalalignment="center", 
                arrowprops=dict(arrowstyle='-|>', color='black', lw=SMALL_LINE_WIDTH), fontsize=LEGEND_FONT_SIZE_SMALL//1.5)

        ax.spines['right'].set_visible(False)
        ax.spines['top'].set_visible(False)
        plt.xticks(x_ticks)
        #plt.ylim(0, 3000)
        #plt.xlim(0, sec)
        ax.set_ylabel("Response Time" + ' (%ss)' % r'$\mu$')
        ax.set_xlabel("Time (s)")
        plt.legend(loc='best')
        #plt.grid(True)
        plt.tight_layout()
        out_name_ext = ""
        if is_wide:
            out_name_ext = "_wide"
        plt.savefig(latsFile + out_name_ext + '.png', ext='png', bbox_inches="tight")
        plt.savefig(latsFile + out_name_ext + '.pdf', ext='pdf', bbox_inches="tight")

    latsFile = sys.argv[1]
    getLatPct(latsFile)
