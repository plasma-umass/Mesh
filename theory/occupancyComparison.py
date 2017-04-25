# -*- coding: utf-8 -*-
"""
Created on Tue Jan 31 19:49:02 2017

@author: devd
"""

from __future__ import division
from createRandomString import *
from makeGraph import *
#from greedyMesher import *
import matplotlib.pyplot as plt
import matplotlib.patches as mpatches
import networkx as nx
import numpy as np
from compute_exp_Y import compute_exp_Y, compute_degree_bound, compute_isolated_edge_bound
from choose import compute_q

def experiment(length, ones_range_min, ones_range_max, reps, numStrings):
    constant_strings = []
    independent_strings = []
    ones = []
    constant_occupancy_avg = []
    constant_occupancy_std_dev = []
    indep_occupancy_avg = []
    indep_occupancy_std_dev = []
    qs = []
    constant_edges = []
    independent_edges = []
    
    for numOnes in range(ones_range_min, ones_range_max+1):
        ones.append(numOnes)
        q = compute_q(length, numOnes)
        qs.append(q)
        freed_pages_constant = []
        freed_pages_independent = []
        const_edges = []
        indep_edges = []
        for iterations in range (reps):
            for i in range(numStrings):
               constant_strings.append(createRandomString(length, numOnes))
               
            graph = makeGraph(constant_strings)
            const_edges.append(graph.number_of_edges())
            frdpgs_constant = len(nx.max_weight_matching(graph))/2
            perc = (frdpgs_constant/numStrings)*100
            freed_pages_constant.append(perc)
            
#include only q or numOnes by name to choose which version of indep random strings you want            
#            independent_strings = createIndependentRandomStrings(length, numStrings, q, numOnes)
            independent_strings = createIndependentRandomStrings(length, numStrings, numOnes = numOnes)
            graph = makeGraph(independent_strings)
            indep_edges.append(graph.number_of_edges())
            frdpgs_indep = len(nx.max_weight_matching(graph))/2
            perc = (frdpgs_indep/numStrings)*100
            freed_pages_independent.append(perc)
        
            
            constant_strings = []
            independent_strings = []
            
        
        m = np.asarray(freed_pages_constant)
#        raw.append(freed_pages_constant)
        m_a = np.mean(m)
        constant_occupancy_avg.append(m_a)
        m_s = np.std(m)
        constant_occupancy_std_dev.append(m_s)
        
        m = np.asarray(const_edges)
        constant_edges.append(np.mean(m))
        
        m = np.asarray(freed_pages_independent)
#        raw.append(freed_pages_constant)
        m_a = np.mean(m)
        indep_occupancy_avg.append(m_a)
        m_s = np.std(m)
        indep_occupancy_std_dev.append(m_s)
        
        m = np.asarray(indep_edges)
        independent_edges.append(np.mean(m))
        
    return ones, constant_occupancy_avg, constant_occupancy_std_dev, indep_occupancy_avg, indep_occupancy_std_dev, constant_edges, independent_edges, qs


def plot_it(length, ones_range_min, ones_range_max, reps, numStrings):
    ones, constant_occupancy_avg, constant_occupancy_std_dev, indep_occupancy_avg, indep_occupancy_std_dev, constant_edges, independent_edges, qs = experiment(length, ones_range_min, ones_range_max, reps, numStrings)
    
    plt.errorbar(np.asarray(ones), np.asarray(constant_occupancy_avg), np.asarray(constant_occupancy_std_dev), markersize=3, lw=1, fmt='-o')
    plt.errorbar(np.asarray(ones), np.asarray(indep_occupancy_avg), np.asarray(indep_occupancy_std_dev), markersize=3, lw=1, fmt='-o')
    plt.ylim([0,60])
#    plt.xlim([10, 16])
    plt.ylabel('Percentage of pages freed')
    plt.xlabel('Number of objects per page')
    blue_patch = mpatches.Patch(color='blue', label='const occupancy')
    green_patch = mpatches.Patch(color = 'green', label = 'indep occupancy')
#    red_patch = mpatches.Patch(color = 'red', label = 'q')
    plt.legend(handles=[blue_patch, green_patch])
#    plt.legend(handles=[blue_patch, green_patch, red_patch])
    plt.title('CONSTANT VS INDEPENDENT OCCUPANCY \n{}-object pages, {} pages. p = n/b'.format(length, numStrings))
    plt.show()
#    plt.savefig('constvindep_{},{}'.format(length, numStrings) + '.png', dpi = 1000)
    plt.close()
    
#    plt.errorbar(np.asarray(ones), np.asarray(constant_edges), np.asarray(np.zeros(len(ones))), markersize=3, lw=1, fmt='-o')
#    plt.errorbar(np.asarray(ones), np.asarray(independent_edges), np.asarray(np.zeros(len(ones))), markersize=3, lw=1, fmt='-o')
#    plt.show()
        
        
if __name__ == '__main__':        
    #length = [32,64]
    length = [64]
    ones_range_min = 1
    ones_range_max = 32
    reps = 10
    #numStrings = [80,100,150,200]
    numStrings= [80]
    
    
    for l in length:
        for n in numStrings:
            plot_it(l, ones_range_min, int(l/2), reps, n)
    #        plot_it(l, 10, 13, 10, n)
            print 'const v indep plot {},{} done'.format(l,n)
        
        
        
        
        
        
        
        
        