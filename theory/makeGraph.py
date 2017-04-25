# -*- coding: utf-8 -*-
"""
Created on Sat Feb 27 23:07:03 2016

@author: devd
"""
import numpy as np
from scipy.sparse import csr_matrix
import networkx as nx

def makeAdjacencyMatrix(strings, sparse):
    nums = [long(s, base=2) for s in strings]
    dim = len(strings)
    graph = np.zeros((dim,dim))
    for i in range(dim):
        for j in range(dim):
            if i == j:
                continue
            num = nums[i]
            num2 = nums[j]
            if num & num2 == 0:
                graph[i,j] = 1
    if sparse:
        graph = csr_matrix(graph)
    return graph




def makeGraph(strings):
    nums = [long(s, base=2) for s in strings]
    dim = len(strings)
    g = np.zeros((dim,dim))
    for i in range(dim):
        for j in range(dim):
            if i == j:
                continue
            num = nums[i]
            num2 = nums[j]
            if num & num2 == 0:
                g[i,j] = 1
    graph = nx.Graph(g)
    return graph
    
    
#g = makeGraph(['000','001','111','101', '110', '010'])
#nx.draw(g)
#degrees = g.degree(g.nodes())
#print max(degrees.values())
#print min(degrees.values())
#print degrees
    
    
    
    
    
    
#g_c = nx.complement(g)
#color = nx.greedy_color(g_c)
#print color
#i = 0
#for key, value in color.iteritems():
#    i = max(i, value)
#print i + 1
