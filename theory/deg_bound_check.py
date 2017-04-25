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
import logging

logging.getLogger('').handlers = []
logging.basicConfig(level=logging.DEBUG, format='%(asctime)s - %(levelname)s - %(message)s')

def compute_actual_improved_deg_bound(graph):
    edgeList = graph.edges()
    lower_bound = 0
    for edge in edgeList:
        deg1 = graph.degree(edge[0])
        deg2 = graph.degree(edge[1])
#        if deg1 == deg2 == 1:
#            lower_bound += 1
#        elif deg1 == 1 and deg2 == 2:
#            lower_bound+=.5
#        elif deg1 == 1:
#            lower_bound += (1.0/deg2)
#        elif deg2 ==1:
#            lower_bound += (1.0/deg1)
#        elif deg1 == 2 and deg2 == 3:
#            lower_bound += 1.0/3.0
        if deg1 == 2 and deg2 == 2:
            lower_bound += 1/3.0
        else:
#            lower_bound += min((1.0/(deg1+1)),(1.0/(deg2+1)))
            lower_bound += min((1.0/(deg1)),(1.0/(deg2)))
    return lower_bound

def experiment(length, ones_range_min, ones_range_max, reps, numStrings):
    strings = []


    for numOnes in range(ones_range_min, ones_range_max+1):
        for iterations in range (reps):
#            for i in range(numStrings):
#               strings.append(createRandomString(length, numOnes))
            strings = createIndependentRandomStrings(length = length, numStrings = numStrings, numOnes = numOnes)
               
            graph = makeGraph(strings)
            max_matching = len(nx.max_weight_matching(graph))/2
            lower_bound = compute_actual_improved_deg_bound(graph)
            if max_matching+.0001 < lower_bound:
                print 'Max matching {} but lower bound {} for length {} strings, occupancy {}, {} total strings'.format(max_matching, lower_bound, length, numOnes, numStrings)
                print nx.triangles(graph)
                print graph.degree()                
#                nx.draw(graph)
            strings = []
    
def experiment2(numNodes):
    graph = nx.complete_graph(numNodes)
    max_matching = len(nx.max_weight_matching(graph))/2
    lower_bound = compute_actual_improved_deg_bound(graph)
    if max_matching+.0001 < lower_bound:
        print 'Max matching {} but lower bound {} for complete graph on {} nodes'.format(max_matching, lower_bound, numNodes)
    
    
def plot_it(length, ones_range_min, ones_range_max, reps, numStrings):
    experiment(length, ones_range_min, ones_range_max, reps, numStrings)



if __name__ == '__main__': 
#    experiment2(101)
    
    length = [32]
    ones_range_min = 1
#    ones_range_min = 8
    ones_range_max = 32
    reps = 100
    numStrings= [20]
    
    start = time.time()
    t = time.localtime()
    logging.info('deg bound experiment with lengths {}, num strings {} started'.format(length, numStrings))
    for l in length:
        for n in numStrings:
            plot_it(l, ones_range_min, int(l/2), reps, n)
            logging.info('completed length {} num strings {}'.format(l, n))
    end = time.time()
    print 'done in {} seconds'.format(end-start)
#    graph = makeGraph(['0101','1100','0011','1010'])
#    nx.draw(graph)
#    print compute_actual_improved_deg_bound(graph)