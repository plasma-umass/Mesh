# -*- coding: utf-8 -*-
"""
Created on Fri Apr 15 15:25:44 2016

@author: devd
"""

from __future__ import division
from createRandomString import *
from makeGraph import *
import matplotlib.pyplot as plt
import matplotlib.patches as mpatches
import networkx as nx
import numpy as np
import time
from compute_exp_Y import compute_exp_Y, compute_degree_bound, compute_isolated_edge_bound, compute_degreeplusone_bound, compute_improved_degreeplusone_bound
from choose import compute_q

def experiment(length, ones_range_min, ones_range_max, reps, numStrings):
    strings = []
    ones = []
    maxmatch_avg = []
    maxmatch_std_dev = []
#    greedymatch_avg = []
#    greedymatch_std_dev = []
    y_estimate = []
    justy = []
    raw = []
    qs = []


    for numOnes in range(ones_range_min, ones_range_max+1):
        ones.append(numOnes)
        q = compute_q(length, numOnes)
        qs.append(q*100)
#        qs.append(compute_q(length, numOnes)*100)
        freed_pages_maxmatching = []
        freed_pages_greedymatching = []
        for iterations in range (reps):
            for i in range(numStrings):
               strings.append(createRandomString(length, numOnes))
#            strings = createIndependentRandomStrings(length = length, numStrings = numStrings, q = q)
               
            graph = makeGraph(strings)
            frdpgs_maxmatching = len(nx.max_weight_matching(graph))/2
            perc = (frdpgs_maxmatching/numStrings)*100
            freed_pages_maxmatching.append(perc)
            
            strings = []
            
                    
            
        m = np.asarray(freed_pages_maxmatching)
        raw.append(freed_pages_maxmatching)
        m_a = np.mean(m)
        maxmatch_avg.append(m_a)
        m_s = np.std(m)
        maxmatch_std_dev.append(m_s)
        
#        y = compute_exp_Y(length, numOnes, numStrings)
#        y = compute_degreeplusone_bound(length, numOnes, numStrings)
        y = compute_improved_degreeplusone_bound(length, numOnes, numStrings)
        
#        y_est_raw = max(y,compute_degree_bound(length, numOnes, numStrings),compute_isolated_edge_bound(length, numOnes, numStrings))
#        y_est_raw = compute_isolated_edge_bound(length, numOnes, numStrings)
#        y_est_raw = compute_degree_bound(length, numOnes, numStrings)
#        y_est_raw = y
        yperc = (math.floor(y)/numStrings)*100
        y_estimate.append(yperc)

        
#    mistakes = {}
#    for i in range(len(raw)):
#        oops = []
#        for entry in raw[i]:
#            if entry < y_estimate[i]:
#                oops.append(entry)
#        mistakes[i+1] = oops
#    print 'mistakes:'
#    print mistakes
    
    
    
    
    
#    use this version of mistakes
    mistakes = {}
    for i in range(len(raw)):
        oops = 0
        for entry in raw[i]:
            if entry < y_estimate[i]:
                oops += 1
        mistakes[i+1] = oops
    print 'mistakes:'
    print mistakes    
    
    print 'E[Y]:'
    ey = {}
    for i in range(len(y_estimate)):
        ey[i+1] = y_estimate[i]
    print ey
#                
        
#        yperc = (y/numStrings)*100
#        justy.append(yperc)
        
#        c = np.asarray(freed_pages_greedymatching)
#        c_a = np.mean(c)
#        greedymatch_avg.append(c_a)
#        c_s = np.std(c)
#        greedymatch_std_dev.append(c_s)
    
    
        
    return ones, maxmatch_avg, maxmatch_std_dev, y_estimate, justy, qs
    
    
def plot_it(length, ones_range_min, ones_range_max, reps, numStrings):
    ones, match_avg, match_std_dev, y_estimate, justy, qs = experiment(length, ones_range_min, ones_range_max, reps, numStrings)
    
#    print y_estimate
    plt.errorbar(np.asarray(ones), np.asarray(match_avg), np.asarray(match_std_dev), markersize=3, lw=1, fmt='-o')
#    plt.errorbar(np.asarray(ones), np.asarray(color_avg), np.asarray(color_std_dev), markersize=3, lw=1, fmt='-o')
    plt.errorbar(np.asarray(ones), np.asarray(y_estimate), np.zeros(len(ones)), markersize=3, lw=1, fmt='-o')
#    plt.errorbar(np.asarray(ones), np.asarray(justy), np.zeros(len(ones)), markersize=3, lw=1, fmt='-o')
#    plt.plot(np.asarray(ones), y_estimate, markersize = 3, lw=1, fmt='o')
#    plt.errorbar(np.asarray(ones), np.asarray(qs), np.zeros(len(ones)), markersize=3, lw=1, fmt='-o')
    plt.ylim([0,60])
#    plt.xlim([10, 14])
    plt.ylabel('Percentage of pages freed')
    plt.xlabel('Number of objects per page')
    blue_patch = mpatches.Patch(color='blue', label='max matching')
    green_patch = mpatches.Patch(color = 'green', label = 'lower bound')
    red_patch = mpatches.Patch(color = 'red', label = 'q')
    plt.legend(handles=[blue_patch, green_patch])
#    plt.legend(handles=[blue_patch, green_patch, red_patch])
    plt.title('MAX MATCHING VS LOWER BOUND \n{}-object pages, {} pages'.format(length, numStrings))
#    plt.show()
#    plt.savefig('maxvE[Y]{},{}'.format(length, numStrings) + '.png', dpi = 1000)
    plt.savefig('maxvdeg+1imp++{},{}'.format(length, numStrings) + '.png', dpi = 1000)
#    plt.savefig('manystrings.png', dpi = 1000)
    plt.close()


if __name__ == '__main__':    
    #length = [32,64]
    length = [32]
    ones_range_min = 1
    ones_range_max = 32
    reps = 10
#    numStrings = [80,100,150,200]
    numStrings= [80]
    
    
    start = time.time()
    for l in length:
        for n in numStrings:
            plot_it(l, ones_range_min, int(l/2), reps, n)
    #        plot_it(l, 10, 13, 10, n)
            print 'max match vs E[Y] plot {},{} done'.format(l,n)
    end = time.time()
    print('making this took {} seconds'.format(end-start) )