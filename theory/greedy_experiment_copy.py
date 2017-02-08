# -*- coding: utf-8 -*-
"""
Created on Sat Feb 27 21:32:33 2016

@author: devd
"""
from __future__ import division
from createRandomString import *
from meshers import *
import matplotlib.pyplot as plt
from matplotlib.backends.backend_pdf import PdfPages


length = 32
ones_range_min = 4
ones_range_max = 16
reps = 10
numStrings = 100

strings = []
ones = []
numUnmatched = []
perc = []
for numOnes in range(ones_range_min, ones_range_max+1):
    for iterations in range (reps):
        for i in range(numStrings):
            strings.append(createRandomString(length, numOnes))
        b, unmatched = greedyMesher(strings)
        ones.append(numOnes)
        percentage = (len(unmatched)/numStrings)*100
        numUnmatched.append(len(unmatched))
        perc.append(percentage)
        strings = []
plt.plot(ones, numUnmatched,'ro')
plt.ylabel('Number of unmatched strings')
plt.xlabel('Number of ones per string')
plt.title('GREEDY FIRST-MATCH MESHING RESULTS \n{}-bit strings, {} trials per x value'.format(length, reps))
#plt.show()


plt.plot(ones, perc,'ro')
plt.ylabel('Percentage of unmatched strings')
plt.xlabel('Number of ones per string')
plt.title('GREEDY FIRST-MATCH MESHING RESULTS \n{}-bit strings, {} trials per x value, {} strings per trial'.format(length, reps, numStrings))
plt.show()
#plt.savefig('test.png', dpi = 1000)
