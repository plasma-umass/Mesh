# -*- coding: utf-8 -*-
"""
Created on Fri Feb 03 09:45:30 2017

@author: devd
"""

from __future__ import division
from createRandomString import *
from makeGraph import *
from meshers import *
import networkx as nx
import numpy as np
import os


def dumpStrings(strings, length, occupancy, numStrings, meshingMethod, result):
    """Dumps the generated string set in a text file in a subdirectory."""
    if length == 0:
        file_name = 'provided,{}.txt'.format(meshingMethod)
    else:
        file_name = '{},{},{},{}.txt'.format(length, occupancy, numStrings, meshingMethod)
        
    script_dir = os.path.dirname(os.path.abspath(__file__))
    dest_dir = os.path.join(script_dir, 'dumps')
    try:
        os.makedirs(dest_dir)
    except OSError:
        pass # already exists
    path = os.path.join(dest_dir, file_name)
    output = open(path, 'wb')
    for s in strings:
        output.write('{}\n'.format(s))
    output.write('-{}'.format(result))
    output.close


def experiment(length = 0, occupancy = 0, numStrings = 0, meshingMethod = "dumb", attempts = 0, stringList = None):
    """Produces a set of random binary strings with specified length and expected
    occupancy.  Then meshes them using the specified meshing method and returns
    the percentage of total pages freed.  Also dumps the generated string set
    to a text file for comparison with other implementations.
    
    Meshing methods:
    dumb = attempt to mesh string 1 w/ string 2 etc. 1 pass only.
    random = attempt to mesh 2 randomly chosen strings.  specify total number of
        allowed attempts.
    maxMatching = converts string set to graph and computes the max matching of
        graph.  This gives the optimal meshing assuming we only mesh pairs.
    optimal = converts string set to the graph and computes the minimum clique
        cover.  This gives the optimal meshing without the "pairs only" constraint.
    """
    
    #note that occupancy is currently the number of ones expected per string, rather than a percentage.
    if stringList == None:
        if length == 0 or numStrings == 0:
            raise Exception('must give a string list or specify length, occupancy, numStrings for generation')
        strings = createIndependentRandomStrings(length, numStrings, numOnes = occupancy)
    else:
        strings = stringList
    
    if meshingMethod == "dumb":
        result = simpleMesher(strings)
        
    elif meshingMethod == "random":
        if attempts == 0:
            raise Exception('must specify number of attempts for random method')
        meshed = randomMesher(strings, attempts)
        result = int(len(meshed)/2)
        
    elif meshingMethod == "maxMatching":
        result = maxMatchingMesher(strings)
        
    elif meshingMethod == "optimal":
        result = optimalMesher(strings)
    
    else:
        print 'hi'
        
    dumpStrings(strings, length, occupancy, numStrings, meshingMethod, result)
    


if __name__ == '__main__':
#    experiment(length = 10, occupancy = 3, numStrings = 10, meshingMethod = "random", attempts = 100)
#    experiment(stringList = ['0000000000','1111111111','0101010101','1010101010'])

    lengths = [32]
    occupancies = range(1,10)
    numsStrings = [80]
    for i in lengths:
        for j in occupancies:
            for k in numsStrings:
                experiment(length = i, occupancy = j, numStrings = k)
    
        