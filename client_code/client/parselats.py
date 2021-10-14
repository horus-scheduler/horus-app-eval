#!/usr/bin/python3

import sys
import os
import numpy as np
from scipy import stats

if __name__ == '__main__':
    def getLatPct(latsFile):
        assert os.path.exists(latsFile)

    
        result = values = np.genfromtxt(latsFile, delimiter='\n')
        result = result[1000:]
        sjrnTimes = [l/1e3 for l in result]
        #print(sjrnTimes)
        p99 = np.percentile(sjrnTimes, 99)
        p50 = np.percentile(sjrnTimes, 50)
        maximum = max(sjrnTimes)
        print("99th percentile: " + str(p99))
        print("median: " + str(p50))
        #print(maximum)
        print("mean: " + str(np.mean(sjrnTimes)))
        
    latsFile = sys.argv[1]
    getLatPct(latsFile)
