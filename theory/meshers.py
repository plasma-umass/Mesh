# -*- coding: utf-8 -*-
"""
Created on Fri Feb 03 10:23:57 2017

@author: devd
"""

import random 
import numpy as np
from makeGraph import *
import networkx as nx


def simpleMesher(strings):
    """Attempts to mesh the first string in the list with the second, etc.  Returns
    number of successful meshes."""
    meshes = 0
    #try to mesh each string 
    for i in range(0, len(strings), 2):
        str1 = strings[i]
        str2 = strings[i+1]
        num = [int(x) for x in list(str1)]
        num2 = [int(x) for x in list(str2)]
        if np.dot(num, num2) == 0:
            meshes += 1
    return meshes


def randomMesher(strings, attempts):
    """DEPRECATED"""
    s = [x for x in strings]
    matched_strings = []
    for i in range(attempts):
        pair = random.sample(s,2)
        str1 = pair[0]
        str2 = pair[1]
        num = [int(x) for x in list(str1)]
        num2 = [int(x) for x in list(str2)]
        if np.dot(num, num2) == 0:
            #print('removing {} and {}'.format(str1, str2))
            matched_strings.append(str1)
            matched_strings.append(str2)
            s.remove(str1)
            s.remove(str2)
            if len(s) < 2:
                return matched_strings
    return matched_strings
    

def greedyMesher(strings):
    """DEPRECATED
    Meshes a list of strings using a greedy first-match technique.  Returns 
    the number of unmatched strings after available matches are exhausted."""
    s = strings
    matched_strings = []
    unmatched_strings = []
    matched = []
    for i in range(len(s)):
        for j in range(i+1, len(s)):
            if i not in matched and j not in matched:
                num = [int(x) for x in list(s[i])]
                num2 = [int(x) for x in list(s[j])]
                if np.dot(num, num2) == 0:
                    matched.append(i)
                    matched.append(j)
    matched_strings += [s[x] for x in matched]
    unmatched_strings += [s[x] for x in range(len(s)) if x not in matched]
                    
    return matched_strings, unmatched_strings
    
def maxMatchingMesher(strings):
    graph = makeGraph(strings)
    meshes = len(nx.max_weight_matching(graph))/2
    return meshes
    
def color_counter(graph):
    color = nx.greedy_color(graph)
    i = 0
    for key, value in color.iteritems():
        i = max(i, value)
    return i+1    
    
def optimalMesher(strings):
    graph = makeGraph(strings)
    graph_c = nx.complement(graph)
    meshes = len(strings) - color_counter(graph_c)
    return meshes