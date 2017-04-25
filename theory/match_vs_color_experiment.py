# -*- coding: utf-8 -*-
"""
Created on Sun Feb 28 22:44:37 2016

@author: devd
"""

from __future__ import division
from createRandomString import *
from makeGraph import *
import matplotlib.pyplot as plt
import matplotlib.patches as mpatches
import networkx as nx
import numpy as np


def color_counter(graph):
    color = nx.greedy_color(graph)
    i = 0
    for key, value in color.iteritems():
        i = max(i, value)
    return i+1

def experiment(length, ones_range_min, ones_range_max, reps, numStrings):
    strings = []
    ones = []
    match_avg = []
    match_std_dev = []
    color_avg = []
    color_std_dev = []

    for numOnes in range(ones_range_min, ones_range_max+1):
        ones.append(numOnes)
        freed_pages_matching = []
        freed_pages_coloring = []
        for iterations in range (reps):
            for i in range(numStrings):
               strings.append(createRandomString(length, numOnes))
#            strings = createIndependentRandomStrings(length, numStrings, numOnes = numOnes)
               
            graph = makeGraph(strings)
            frdpgs_matching = len(nx.max_weight_matching(graph))/2
            perc = (frdpgs_matching/numStrings)*100
            freed_pages_matching.append(perc)
            
            graph_c = nx.complement(graph)
            frdpgs_coloring = numStrings - color_counter(graph_c)
            perc = (frdpgs_coloring/numStrings)*100
            freed_pages_coloring.append(perc)
            
            strings = []
        m = np.asarray(freed_pages_matching)
        m_a = np.mean(m)
        match_avg.append(m_a)
        m_s = np.std(m)
        match_std_dev.append(m_s)
        
        c = np.asarray(freed_pages_coloring)
        c_a = np.mean(c)
        color_avg.append(c_a)
        c_s = np.std(c)
        color_std_dev.append(c_s)
        
    return ones, match_avg, match_std_dev, color_avg, color_std_dev


def plot_it(length, ones_range_min, ones_range_max, reps, numStrings):
    ones, match_avg, match_std_dev, color_avg, color_std_dev = experiment(length, ones_range_min, ones_range_max, reps, numStrings)
    
    plt.errorbar(np.asarray(ones), np.asarray(match_avg), np.asarray(match_std_dev), markersize=3, lw=1, fmt='-o')
    plt.errorbar(np.asarray(ones), np.asarray(color_avg), np.asarray(color_std_dev), markersize=3, lw=1, fmt='-o')
    plt.ylim([0,100])
    plt.ylabel('Percentage of pages freed')
    plt.xlabel('Number of objects per page')
    blue_patch = mpatches.Patch(color='blue', label='matching')
    green_patch = mpatches.Patch(color = 'green', label = 'clique')
    plt.legend(handles=[blue_patch, green_patch])
    plt.title('MAX MATCHING VS MIN CLIQUE COVER MESHING RESULTS \n{}-object pages, {} pages'.format(length, numStrings))
#    plt.show()
    plt.savefig('{}m{}ind'.format(length, numStrings) + '.png', dpi = 1000)
#    plt.close()

length = [64]
ones_range_min = 1
ones_range_max = 32
reps = 10
#numStrings = [80,100,150,200]
numStrings = [80]

for l in length:
    for n in numStrings:
        plot_it(l, ones_range_min, int(l/2), reps, n)
        print 'match vs color plot {},{} done'.format(l,n)

