# -*- coding: utf-8 -*-
"""
Created on Mon Jun 13 11:46:53 2016

@author: devd
"""

from createRandomString import *
from makeGraph import *
import networkx as nx
import matplotlib.pyplot as plt

def experiment(numOnes, numStrings, length, reps):
    maxdeg = []
    mindeg = []
    for i in range(reps):
        strings = []
        for i in range(numStrings):
               strings.append(createRandomString(length, numOnes))
        graph = makeGraph(strings)
#        nx.draw(graph)
        max = min = graph.degree(0)
#        print 'initial max/min is {}'.format(max)
        for j in range(1, numStrings):
            deg = graph.degree(j)
#            print 'this one has degree {}'.format(deg)
            if deg>max:
                max = deg
            if deg<min:
                min = deg
        maxdeg.append(max)
        mindeg.append(min)
    
    return maxdeg, mindeg


def plot_it(numOnes, numStrings, length, reps):
    maxdeg, mindeg = experiment(numOnes, numStrings, length, reps)
    x = range(reps)
    print maxdeg
    print mindeg
    plt.plot(x, maxdeg)
    plt.plot(x, mindeg)
#    plt.show()
    

plot_it(5, 80, 32, 10)