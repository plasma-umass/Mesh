# -*- coding: utf-8 -*-
"""
Created on Fri Feb 03 10:23:57 2017

@author: devd
"""

import random
import numpy as np
from makeGraph import makeGraph
import networkx as nx
from mesh_util import Splitter, hamming, traverse, occupancySort, formatStrings, simpleGreedyTraverse, greedyTraverse
import time


def simpleMesher(strings, stdOutput=False):
    """
    Attempts to mesh the first string in the list with the second, etc.
    Returns number of successful meshes.
    """
    meshes = 0
    # try to mesh each string
    for i in range(0, len(strings), 2):
        str1 = strings[i]
        str2 = strings[i + 1]
        num = [int(x) for x in list(str1)]
        num2 = [int(x) for x in list(str2)]
        if np.dot(num, num2) == 0:
            meshes += 1
    if stdOutput:
        return 100 * float(meshes) / len(strings)
    return meshes


# def randomMesher(strings, attempts):
#    """DEPRECATED"""
#    s = [x for x in strings]
#    matched_strings = []
#    for i in range(attempts):
#        pair = random.sample(s,2)
#        str1 = pair[0]
#        str2 = pair[1]
#        num = [int(x) for x in list(str1)]
#        num2 = [int(x) for x in list(str2)]
#        if np.dot(num, num2) == 0:
#            #print('removing {} and {}'.format(str1, str2))
#            matched_strings.append(str1)
#            matched_strings.append(str2)
#            s.remove(str1)
#            s.remove(str2)
#            if len(s) < 2:
#                return matched_strings
#    return matched_strings

def randomMesher(strings, attempts, display=False, stdOutput=False):
    length = len(strings[0])
    totalStrings = len(strings)
    strings = [long(string, base=2) for string in strings]
    meshes = []
    for k in range(attempts):
        matched = []
        random.shuffle(strings)
        dim = len(strings)
        for i in range(dim - 2, -2, -2):
            num1 = strings[i]
            num2 = strings[i + 1]
            if num1 & num2 == 0:
                matched.append(i)
        for x in matched:
            meshes.append((strings[x], strings[x + 1]))
        for x in matched:
            del strings[x + 1]
            del strings[x]

    formatstring = "{0:0" + str(length) + "b}"
    meshes = [(formatstring.format(num), formatstring.format(num2))
              for (num, num2) in meshes]
    if display:
        print "meshes:"
        print meshes
    if stdOutput:
        return 100 * float(len(meshes)) / totalStrings
    return len(meshes)


def _greedyMesher(strings, stdOutput=False):
    """DEPRECATED
    Meshes a list of strings using a greedy first-match technique.  Returns 
    the number of matched pairs after available matches are exhausted."""
    s = strings
    matched_strings = []
    unmatched_strings = []
    matched = []
    for i in range(len(s)):
        for j in range(i + 1, len(s)):
            if i not in matched and j not in matched:
                num = [int(x) for x in list(s[i])]
                num2 = [int(x) for x in list(s[j])]
                if np.dot(num, num2) == 0:
                    matched.append(i)
                    matched.append(j)
    matched_strings += [s[x] for x in matched]
    unmatched_strings += [s[x] for x in range(len(s)) if x not in matched]

    if stdOutput == True:
        return 100 * len(matched_strings) / (2 * len(strings))
    else:
        return matched_strings, unmatched_strings


def greedyMesher(strings, stdOutput=False, cutoff=None):
    length = len(strings)
    new_strings = formatStrings(strings)
    meshes = []
    occupancySort(new_strings)
    simpleGreedyTraverse(meshes, new_strings, cutoff)
    if stdOutput:
        return 100 * len(meshes) / length
    else:
        return meshes

# def splitter(strings, length, splitting_string = 0):
#    splitting_strings = []
#    num_splitters = int(math.log(length,2))+1
#    for i in range(1,num_splitters):
#        split_string = ""
#        for j in range(2**(i-1)):
#            split_string = split_string + (("1" * int((length/(2**i)))) + ("0" * (int(length/(2**i)))))
#        splitting_strings.append(split_string)
# if splitting_string >= num_splitters-1:
# return bucket1, bucket2
#    split = splitting_strings[splitting_string]
#    bucket1 = []
#    bucket2 = []
#    for s in strings:
#        diff = hamming(s[0], split)
#        if diff < int(length * 0.5):
#            bucket1.append(s)
#        elif diff  == int(length * 0.5):
#            if random.randint(0,1):
#                bucket1.append(s)
#            else:
#                bucket2.append(s)
#        else:
#            bucket2.append(s)
#    return bucket1, bucket2
#
# def splitAgain(bucket1, bucket2, length, method):
#    try:
#        new_bucket1, new_bucket2 = splitter(bucket1+bucket2, length, method)
#    except IndexError:
#        return bucket1, bucket2
#    return new_bucket1, new_bucket2


def splittingMesher(strings, attempts, splittingMethod=0, display=False, stdOutput=False, extra=True):
    if display:
        print "using Splitting Mesher"
    length = len(strings[0])
    new_strings = formatStrings(strings)
    splt = Splitter(length)
    bucket1, bucket2 = splt.split(strings=new_strings)

    meshes = []
    for k in range(attempts):
        #        if k == attempts/2:
        #            print "rebucketing at halfway point"
        #            print bucket1, bucket2
        #            bucket1, bucket2 = splt.split(bucket1 = bucket1, bucket2 = bucket2)
        random.shuffle(bucket1)
        random.shuffle(bucket2)
        try:
            #            print bucket1, bucket2, meshes
            done = traverse(meshes, bucket1=bucket1,
                            bucket2=bucket2, extra=extra)
#            print bucket1, bucket2, meshes
#            print 'that was round {}'.format(k)
        except AssertionError:
            #            print "rebucketing because one bucket is empty"
            bucket1, bucket2 = splt.split(bucket1=bucket1, bucket2=bucket2)
            continue
        if done:
            #            print "all done, ending early at attempt {}".format(k)
            break
    if display:
        print "meshes:"
        print meshes
    if stdOutput:
        return 100 * float(len(meshes)) / len(strings)
    return len(meshes)


def randomSplittingMesher(strings, attempts, display=False, stdOutput=False):
    """randomly splits string list into two lists, and then tries to mesh pairs
    between the lists.  for comparison purposes only, not an actual useful meshing
    method."""
    if display:
        print "using random Splitting Mesher"
    bucket1, bucket2 = [], []
    length = len(strings[0])
#    if splittingMethod == "left":
#        splittingString = ("1" * (length/2)) + ("0" * (length/2))
#    elif splittingMethod == "checkers":
#        splittingString = ("10" * (length/2))
    for string in strings:
        s = long(string, base=2)
        if random.randint(0, 1):
            bucket1.append(s)
        else:
            bucket2.append(s)
    formatstring = "{0:0" + str(length) + "b}"
#    print "bucket1:"
#    print [formatstring.format(item) for item in bucket1]
#    print "bucket2:"
#    print [formatstring.format(item) for item in bucket2]
#    print "\n"
#    print "bucket2: {0:08b}\n".format(bucket2)

    meshes = []
    for k in range(attempts):
        random.shuffle(bucket1)
        random.shuffle(bucket2)
#        print "shuffles: {},\n{}".format(bucket1, bucket2)
        dim = min(len(bucket1), len(bucket2))
        if dim == 0:
            break
        matched = []
        if dim == 1:
            #            print "checking {} and {}".format(bucket1[0], bucket2[0])
            num1 = bucket1[0]
            num2 = bucket2[0]
            if num1 & num2 == 0:
                matched.append(0)
        for i in range(dim - 1, 0, -1):
            #            print "checking {} and {}".format(bucket1[i], bucket2[i])
            num1 = bucket1[i]
            num2 = bucket2[i]
            if num1 & num2 == 0:
                matched.append(i)
        for x in matched:
            meshes.append((bucket1[x], bucket2[x]))
        for x in matched:
            del bucket1[x]
            del bucket2[x]
#    meshes = [(num.toBinaryString(), num2.toBinaryString()) for (num, num2) in meshes]
    meshes = [(formatstring.format(num), formatstring.format(num2))
              for (num, num2) in meshes]
    if display:
        print "meshes:"
        print meshes
    if stdOutput:
        return 100 * float(len(meshes)) / len(strings)
    return len(meshes)


def greedySplittingMesher(strings, display=False, std_output=True, cutoff=None):
    """
    Given a list of strings, splits that list into two lists based off
    of a distance measure and then exhaustively checks pairs between
    the two lists for meshes, greedily taking any it finds.  Sorts the
    lists in increasing order of occupancy so sparse/sparse meshes are
    likely to be discovered.  Can specify a cutoff probability below
    which potential meshes will not be considered - this saves a lot
    of time without affecting performance too much.
    """
    if display:
        print "using greedy splitting mesher"

    length = len(strings[0]) # length of each string, e.g. 4 for '0100'
    start = time.time()
    new_strings = formatStrings(strings)
    splt = Splitter(length)
    bucket1, bucket2 = splt.split(strings=new_strings)
    # print "preliminaries took {}".format(time.time()-start)

    start = time.time()
    meshes = []
    # sorts buckets into low -> high occupancy
    occupancySort(bucket1)
    occupancySort(bucket2)
    # print "sorting took {}".format(time.time()-start)

    start = time.time()
    done = greedyTraverse(meshes, bucket1=bucket1,
                          bucket2=bucket2, cutoff=cutoff)
    # print "traversal took {}".format(time.time()-start)

    if display:
        print "meshes:"
        print meshes
    if std_output:
        return 100 * float(len(meshes)) / len(strings)
    else:
        return len(meshes)


def doubleSplittingMesher(strings, attempts, display=False, stdOutput=False):
    """This function is temporary.  I will soon merge it with splittingMesher to allow for arbitrary levels of splitting
    in the same function."""
    if display:
        print "using double Splitting Mesher"
    buckets = [[], []], [[], []]
    length = len(strings[0])
    numStrings = len(strings)
    splittingString1 = ("1" * (length / 2)) + ("0" * (length / 2))
    splittingString2 = ("10" * (length / 2))
    for string in strings:
        s = long(string, base=2)
        diff = hamming(string, splittingString1)
        diff2 = hamming(string, splittingString2)

        if diff < int(length * 0.5):
            id1 = 0
        elif diff == int(length * 0.5):
            if random.randint(0, 1):
                id1 = 0
            else:
                id1 = 1
        else:
            id1 = 1

        if diff2 < int(length * 0.5):
            id2 = 0
        elif diff == int(length * 0.5):
            if random.randint(0, 1):
                id2 = 0
            else:
                id2 = 1
        else:
            id2 = 1

        buckets[id1][id2].append(s)
    formatstring = "{0:0" + str(length) + "b}"
    for layer in buckets:
        for thing in layer:
            print len(thing)
#    print buckets

    meshes = []

    check1 = True
    check2 = True
    for k in range(attempts):
        dim1 = min(len(buckets[0][0]), len(buckets[1][1]))
        dim2 = min(len(buckets[0][1]), len(buckets[1][0]))
#        print dim1, dim2
        if dim1 == 0:
            if check1:
                print 'found meshes for everything in set 1, so stopped after {} attempts'.format(k)
                check1 = False
        else:
            matched1 = []
            if dim1 == 1:
                num1 = buckets[0][0][0]
                num2 = buckets[1][1][0]
                if num1 & num2 == 0:
                    matched1.append(0)
            for i in range(dim1 - 1, 0, -1):
                num1 = buckets[0][0][i]
                num2 = buckets[1][1][i]
                if num1 & num2 == 0:
                    matched1.append(i)
            for x in matched1:
                meshes.append((buckets[0][0][x], buckets[1][1][x]))
            for x in matched1:
                del buckets[0][0][x]
                del buckets[1][1][x]
        if dim2 == 0:
            if check2:
                print 'found meshes for everything in set 2, so stopped after {} attempts'.format(k)
                check2 = False
        else:
            matched2 = []
            if dim2 == 1:
                num1 = buckets[0][1][0]
                num2 = buckets[1][0][0]
                if num1 & num2 == 0:
                    matched2.append(0)
            for i in range(dim2 - 1, 0, -1):
                num1 = buckets[0][1][i]
                num2 = buckets[1][0][i]
                if num1 & num2 == 0:
                    matched2.append(i)
            for x in matched2:
                meshes.append((buckets[0][1][x], buckets[1][0][x]))
            for x in matched2:
                del buckets[0][1][x]
                del buckets[1][0][x]

    meshes = [(formatstring.format(num), formatstring.format(num2))
              for (num, num2) in meshes]
    if display:
        print "meshes:"
        print meshes
    if stdOutput:
        return 100 * float(len(meshes)) / len(strings)
    return len(meshes)


def maxMatchingMesher(strings, stdOutput=False):
    """Converts the string set into a meshing graph and finds the maximum matching on said graph."""
    graph = makeGraph(strings)
    meshes = len(nx.max_weight_matching(graph)) / 2
    if stdOutput:
        return 100 * float(meshes) / len(strings)
    return meshes


def color_counter(graph):
    """interprets a coloring on a graph as a meshing."""
    color = nx.greedy_color(graph)
    i = 0
    for key, value in color.iteritems():
        i = max(i, value)
    return i + 1


def optimalMesher(strings, stdOutput=False):
    """Converts the string set into a meshing graph and finds a greedy coloring on the complement of said graph."""
    graph = makeGraph(strings)
    graph_c = nx.complement(graph)
    meshes = len(strings) - color_counter(graph_c)
    if stdOutput:
        return 100 * float(meshes) / len(strings)
    return meshes


def mesherRetrieve(identifier):
    fetcher = {"simple": (simpleMesher),
               "dumb": (randomMesher),
               "greedy": (greedyMesher),
               "split": (splittingMesher),
               "greedysplit": (greedySplittingMesher),
               "doubsplit": (doubleSplittingMesher),
               "randsplit": (randomSplittingMesher),
               "maxmatch": (maxMatchingMesher),
               "color": (optimalMesher)
               }
    return fetcher[identifier]


if __name__ == '__main__':
    #    print splittingMesher(["00000001", "11111110", "11100000", "00000111"], 10, display = True)
    #    print splitter([(("1" * 16),0)], 16)
    #    print greedySplittingMesher(["00000001", "11111110", "11100000", "00000111"], display = True, stdOutput = False)

    #    meshes = []
    #    strings = formatStrings(["00000001", "11111110", "11100000", "00000111"])
    #    simpleGreedyTraverse(meshes, strings)
    #    print meshes, strings

    print greedyMesher(["00000001", "11111110", "11100000", "00000111"])
