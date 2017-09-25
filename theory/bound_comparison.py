# -*- coding: utf-8 -*-
"""
Created on Wed Feb 01 19:18:42 2017

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
from compute_exp_Y import compute_exp_Y, compute_degree_bound, compute_isolated_edge_bound, compute_degreeplusone_bound
from choose import compute_q

def experiment(length, ones_range_min, ones_range_max, reps, numStrings):
    ones = []
    y_estimate = []
    deg_estimate = []
    
    for numOnes in range(ones_range_min, ones_range_max+1):
        ones.append(numOnes)
        
        y = compute_exp_Y(length, numOnes, numStrings)
        yperc = (math.floor(y)/numStrings)*100
        y_estimate.append(yperc)
        
        deg = compute_degreeplusone_bound(length, numOnes, numStrings)
        degperc = (math.floor(deg)/numStrings)*100
        deg_estimate.append(degperc)
        
    return ones, y_estimate, deg_estimate

def plot_it(length, ones_range_min, ones_range_max, reps, numStrings):
    ones, y_estimate, deg_estimate = experiment(length, ones_range_min, ones_range_max, reps, numStrings)
    
    plt.errorbar(np.asarray(ones), np.asarray(y_estimate), np.zeros(len(ones)), markersize = 3, lw=1, fmt='o')
    plt.errorbar(np.asarray(ones), np.asarray(deg_estimate), np.zeros(len(ones)), markersize = 3, lw=1, fmt='o')
    plt.ylim([0,60])
    plt.ylabel('Percentage of pages freed')
    plt.xlabel('Number of objects per page')
    blue_patch = mpatches.Patch(color='blue', label='E[Y] bound')
    green_patch = mpatches.Patch(color = 'green', label = 'deg+1 bound')
    plt.legend(handles=[blue_patch, green_patch])
    plt.title('E[Y] vs DEG+1 BOUND RESULTS \n{}-object pages, {} pages'.format(length, numStrings))
    plt.show()
#    plt.savefig('E[Y]vsdeg{},{}'.format(length, numStrings) + '.png', dpi = 1000)
    plt.close()
    
if __name__ == '__main__':    
    #length = [32,64]
    length = [32]
    ones_range_min = 1
    ones_range_max = 32
    reps = 10
    #numStrings = [80,100,150,200]
    numStrings= [80]
    
    
    for l in length:
        for n in numStrings:
            plot_it(l, ones_range_min, int(l/2), reps, n)
    #        plot_it(l, 10, 13, 10, n)
            print 'max match vs E[Y] plot {},{} done'.format(l,n)