# -*- coding: utf-8 -*-
"""
Created on Fri Apr 15 17:21:40 2016

@author: devd
"""

from __future__ import division
from createRandomString import *
from meshers import *
import matplotlib.pyplot as plt
import matplotlib.patches as mpatches
import networkx as nx
import numpy as np
import time
from math import log, floor, sqrt

def experiment(length, ones_range_min, ones_range_max, reps, numStrings, attempts):
    strings = []
    ones = []
    randmatch_avg = []
    randmatch_std_dev = []
    greedymatch_avg = []
    greedymatch_std_dev = []

    for numOnes in range(ones_range_min, ones_range_max+1):
        ones.append(numOnes)
        freed_pages_randmatching = []
        freed_pages_greedymatching = []
        for iterations in range (reps):
            for i in range(numStrings):
               strings.append(createRandomString(length, numOnes))
               
            s = [x for x in strings]
            frdpgs_randmatching = len(randomMesher(s,attempts))/2
            perc = (frdpgs_randmatching/numStrings)*100
            freed_pages_randmatching.append(perc)
            
            b, unmatched = greedyMesher(strings)        
            frdpgs_greedymatching = (numStrings - len(unmatched))/2
            perc = (frdpgs_greedymatching/numStrings)*100
            freed_pages_greedymatching.append(perc)
            
            strings = []
        m = np.asarray(freed_pages_randmatching)
        m_a = np.mean(m)
        randmatch_avg.append(m_a)
        m_s = np.std(m)
        randmatch_std_dev.append(m_s)
        
        c = np.asarray(freed_pages_greedymatching)
        c_a = np.mean(c)
        greedymatch_avg.append(c_a)
        c_s = np.std(c)
        greedymatch_std_dev.append(c_s)
        
    return ones, randmatch_avg, randmatch_std_dev, greedymatch_avg, greedymatch_std_dev
    
    
def plot_it(length, ones_range_min, ones_range_max, reps, numStrings, attempts):
    ones, match_avg, match_std_dev, color_avg, color_std_dev = experiment(length, ones_range_min, ones_range_max, reps, numStrings, attempts)
    
    plt.errorbar(np.asarray(ones), np.asarray(match_avg), np.asarray(match_std_dev), markersize=3, lw=1, fmt='-o')
    plt.errorbar(np.asarray(ones), np.asarray(color_avg), np.asarray(color_std_dev), markersize=3, lw=1, fmt='-o')
    plt.ylim([0,60])
    plt.ylabel('Percentage of pages freed')
    plt.xlabel('Number of objects per page')
    blue_patch = mpatches.Patch(color='blue', label='random matching')
    green_patch = mpatches.Patch(color = 'green', label = 'greedy matching')
    plt.legend(handles=[blue_patch, green_patch])
    plt.title('RANDOM MATCHING VS GREEDY MATCHING MESHING RESULTS \n{}-object pages, {} pages, n^2 attempts'.format(length, numStrings))
    plt.show()
#    plt.savefig('randvgreedy{},{},n^2_attempts'.format(length, numStrings) + '.png', dpi = 1000)
    plt.close()
    
#length = [32,64]
length = [32]
ones_range_min = 1
ones_range_max = 32
reps = 10
#numStrings = [80,100,150,200]
numStrings = [80]


start = time.time()
for l in length:
    for n in numStrings:
        #attempts = n
        #attempts = int(floor(n*log(n,2)))
        #attempts = int(floor(n*sqrt(n)))
        #attempts = int(floor(n**1.75))
        attempts = int(floor(n**2))
        print 'num attempts: {}'.format(attempts)
        plot_it(l, ones_range_min, int(l/2), reps, n, attempts)
        print 'rand match vs greedy match plot {},{} done'.format(l,n)
end = time.time()
print('making this took {} seconds'.format(end-start) )