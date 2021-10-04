#!/usr/bin/python3

import sys
import os
import numpy as np
from scipy import stats


class Lat(object):
    def __init__(self, fileName):
        f = open(fileName, 'rb')
        a = np.fromfile(f, dtype=np.uint64)
        self.reqTimes = a.reshape((a.shape[0], 1))
        f.close()

    def parseQueueTimes(self):
        return self.reqTimes[:, 0]

    def parseSvcTimes(self):
        return self.reqTimes[:, 1]

    def parseSojournTimes(self):
        return self.reqTimes[:, 0]


if __name__ == '__main__':
    def getLatPct(typeOfLats, latsFile):
        assert os.path.exists(latsFile)

        #latsObj = Lat(latsFile)
        if typeOfLats == '--latency':
            # qTimes = [l/1e6 for l in latsObj.parseQueueTimes()]
            # svcTimes = [l/1e6 for l in latsObj.parseSvcTimes()]
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
        elif typeOfLats == '--slowdown':
            sjrnTimes = [l for l in latsObj.parseSojournTimes()]

            p99 = stats.scoreatpercentile(sjrnTimes, 99)
            print(p99)
        else:
            print(
                'Please use either "--latency" or "--slowdown" for the type of latency you want to calculate')
            exit(1)

    typeOfLats = sys.argv[1]
    latsFile = sys.argv[2]
    getLatPct(typeOfLats, latsFile)
