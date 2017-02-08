# -*- coding: utf-8 -*-
"""
Created on Sat Feb 27 21:32:33 2016

@author: devd
"""
from __future__ import division
from createRandomString import *
from greedyMesher import *
import matplotlib.pyplot as plt
import numpy as np
import math
from choose import compute_q

#def nCr(n,r):
#    f = math.factorial
#    return f(n) / f(r) / f(n-r)
#
#def compute_q(length, numOnes):
#    result = float((nCr(length-numOnes, numOnes)))/(nCr(length, numOnes))
#    return result
    

def experiment(length, ones_range_min, ones_range_max, reps, numStrings):
    
    strings = []
    ones = []
    avg = []
    stddev = []
    
    for numOnes in range(ones_range_min, ones_range_max+1):
        ones.append(numOnes)
        vals = []
        for iterations in range (reps):
            for i in range(numStrings):
                strings.append(createRandomString(length, numOnes))
            b, unmatched = greedyMesher(strings)        
            pages_freed = (numStrings - len(unmatched))/2
            percentage = (pages_freed/numStrings)*100
            vals.append(percentage)
            strings = []
        v = np.asarray(vals)
        a = np.mean(v)
        avg.append(a)
        s = np.std(v)
        stddev.append(s)
    return ones, avg, stddev

def plot_it(length, ones_range_min, ones_range_max, reps, numStrings):

    ones, avg, stddev = experiment(length, ones_range_min, ones_range_max, reps, numStrings)
        
    q = [compute_q(length, x) for x in ones]
    
    plt.errorbar(np.asarray(q), np.asarray(avg), np.asarray(stddev), markersize=3, lw=1, fmt='-o')
    plt.ylim([0,60])
    plt.ylabel('Percentage of pages freed')
    plt.xlabel('q (probability of 2 pages meshing)')
    plt.title('GREEDY FIRST-MATCH MESHING RESULTS \n{}-object pages, {} pages'.format(length, numStrings))
    plt.show()
    #plt.savefig('{}p{}'.format(length, numStrings) + '.png', dpi = 1000)
    #plt.close()

#length = [32,64]
length = [32]
ones_range_min = 1
ones_range_max = 16
reps = 10
#numStrings = [80,100,150,200]
numStrings = [80,100]

for l in length:
    for n in numStrings:
        plot_it(l, 1, int(l/2), reps, n)
        print 'greedy plot {},{} done'.format(l,n)