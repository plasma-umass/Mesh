# -*- coding: utf-8 -*-
"""
Created on Wed May 31 14:55:29 2017

@author: devd
"""

import meshers
import createRandomString
import compute_exp_Y
import functools
import numpy as np
import matplotlib.pyplot as plt
import matplotlib.patches as mpatches
import time as t
import datetime
import pickle

def stringSetManager(stringSet, length, numStrings, numOnes, q, filename):
    """Converts a identifying string into a string set generating function with the proper parameters."""
    if stringSet == "random":
        if numOnes != -1:
            gen = lambda: createRandomString.createIndependentRandomStrings(length, numStrings, numOnes = numOnes)
        elif q != -1:
            gen = lambda: createRandomString.createIndependentRandomStrings(length, numStrings, q = q)
        else:
            raise Exception("must specify numOnes or q")
    elif stringSet == "const":
        if numOnes != -1:
            gen = lambda: createRandomString.createConstRandomStrings(length, numStrings, numOnes = numOnes)
        else:
            raise Exception("must specify numOnes")
    elif stringSet == "provided":
        raise Exception("not yet implemented")
    else:
        raise Exception("stringSet value {} not defined".format(stringSet))
    return gen
    
def mesherManager(meshingList):
    """Converts indentifying strings into meshing function objects with corresponding
    partial argument values."""
    mesherList = []
    for bundle in meshingList:
        identifier = bundle[0]
        kwargs = bundle[1]
        func = meshers.mesherRetrieve(identifier)
        prepped_mesher = functools.partial(func, **kwargs)
        mesherList.append(prepped_mesher)
    return mesherList

def boundGenerator(boundList):
    """Looks up the appropriate bound function from a given identifying string
    and returns it."""
    bounds = []
    for b in boundList:
        bounds.append(compute_exp_Y.boundRetrieve(b))
    return bounds
    
def computeBounds(bounds, length, numStrings, numOnes):
    """Using the appropriate bound, computes the value of that bound for given 
    parameter values."""
    results = []
    for b in bounds:
        raw_bound = b(length, numOnes, numStrings)
        scaled = (raw_bound/numStrings)*100
        results.append(scaled)
    return results
    
def filenamer(meshingList, length, numStrings, attempts, boundList = None, time = False):
    """Generates an appropriate filename for a plot based on experiment parameters."""
    plotname = ''
    ids = []
    
    for bundle in meshingList:
        identifier = bundle[0]
        ids.append(identifier)
        plotname = plotname + '{}, '
    for b in boundList:
        ids.append(b)
        plotname = plotname + '{}, '
    plotname = plotname.format(*ids)
    plotname = plotname +'comp {}len {}str {}att'
    if time:
        plotname = plotname + ' time'
    plotname += '.png'
    plotname = plotname.format(length, numStrings, attempts)
    
    return plotname

def dumpToFile(means, std_devs, tmeans, tstd_devs, bound_results, id_string):
    """Automatically dumps the results of the run to a file for logging and error recovery."""
    #name dump file with the human-readable time and date of program execution
    timestamp = t.time()
    value = datetime.datetime.fromtimestamp(timestamp)
    run_id = (value.strftime('%Y-%m-%d %H;%M;%S'))
    path = "experiment_raw_results/{}".format(run_id)
    
    with open(path, "wb+") as file:
        data = id_string, means, std_devs, tmeans, tstd_devs, bound_results
        pickle.dump(data, file)
    file.close()

def trial(stringSetGenerator, meshers, time = False):
    """Generates a string set with given function + parameters, then meshes 
    using one or more meshing methods and outputs the size of the mesh for
    each.  Can also optionally output the computation time required for each 
    meshing method.
    Note that the string set generator and meshers are passed to trial as 
    callable function objects (which may have keyword arguments partially
    specified already)."""
    strings = stringSetGenerator()
    mesh_results = []
    time_results = []
    for mesher in meshers:
        start = t.time()
        mesh_results.append(mesher(strings))
        duration = t.time() - start
        #report times in milliseconds
        time_results.append(duration*1000)
    return tuple(mesh_results), tuple(time_results)

def repeatedTrials(stringSet = "random", length = 16, numStrings = 100, numOnes = -1, q = -1, filename = None, meshers = None, reps = 10, time = False):
    """This function performs a series of trials comparing meshing algorithms on different iid string sets.
    It returns numpy arrays of the means and std deviations of meshing sizes for each meshing method.
    It also optionally returns numpy arrays of the mean and std dev times to compute those meshings.
    mesherList takes a list of tuples where the first element is the identifier for mesher, and the second is a list of supplementary kwargs for that
    mesher function call (some meshers require a number of attempts, etc).  """
    gen = stringSetManager(stringSet, length, numStrings, numOnes, q, filename)
    results = np.empty((reps, len(meshers)))
    if time:
        times = np.empty((reps, len(meshers)))
    for i in range(reps):
        result, time_result = trial(gen, meshers, time = time)
        results[i] = np.asarray(result)
        if time:
            times[i] = np.asarray(time_result)
                
    means = np.mean(results, axis = 0)
    std_devs = np.std(results, axis = 0)
    if time:
        tmeans = np.mean(times, axis = 0)
        tstd_devs = np.std(times, axis = 0)
    else:
        tmeans, tstd_devs = None, None
    return means, std_devs, tmeans, tstd_devs
    
#remember to include handling for q later
def experiment(stringSet = "random", length = 16, numStrings = 100, x = range(1,8), filename = None, meshingList = None, boundList = None, reps = 10, time = False):
    """Performs random meshing experiments on a given list of meshing algorithms.  Can optionally compute & record runtime of these algorithms
    as well as the size of their computed meshings.  Must specify properties of random string set (length of strings, number of strings, range of 
    occupancy values, etc."""
    dim = len(x)
    meshers = mesherManager(meshingList)
    tmeans = None
    tstd_devs = None
    bound_results = None
    if time:
        tmeans = np.empty((dim, len(meshingList)))
        tstd_devs = np.empty((dim, len(meshingList)))
    if boundList:
        bounds = boundGenerator(boundList)
        bound_results = np.empty((dim, len(boundList)))
    
    means = np.empty((dim, len(meshingList)))
    std_devs = np.empty((dim, len(meshingList)))
    
    for i in range(dim):
        result = repeatedTrials(stringSet = stringSet, length = length, numStrings = numStrings, numOnes = x[i], meshers = meshers, reps = reps, time = time)
        means[i] = np.asarray(result[0])
        std_devs[i] = np.asarray(result[1])
        if time:
            tmeans[i] = np.asarray(result[2])
            tstd_devs[i] = np.asarray(result[3])
        if boundList:
            results = computeBounds(bounds, length, numStrings, x[i])
            bound_results[i] = np.asarray(results)
    return means, std_devs, tmeans, tstd_devs, bound_results


def plotIt(means, std_devs, tmeans, tstd_devs, bound_results, stringSet = "random", length = 16, numStrings = 100, x = range(1,8), attempts = 10, filename = None, meshingList = None, boundList = [], reps = 10, title = "generated plot", save = False, custom = False, time = False):
    """Runs meshing experiments on specified algorithms for given parameter values.  Then plots this information using matplotlib and either
    displays it or saves to a file.  Set custom = True to disable automatic plot formatting and use your own instead.
    Also automatically dumps the raw data of the last run to a temporary file (in pickled form) so plotting errors don't destroy the results 
    of long-duration experiments."""
        
    for i in range(len(meshingList)):
        plt.errorbar(np.asarray(x), np.asarray(means[:,i]), np.asarray(std_devs[:,i]), markersize=3, lw=1, fmt='-o')
    for i in range(len(boundList)):
        plt.errorbar(np.asarray(x), np.asarray(bound_results[:,i]), np.zeros(len(x)), markersize=3, lw=1, fmt='-o')
    
    plotname = filenamer(meshingList, length, numStrings, attempts, boundList, False)
    if not custom:    
        # fix the y axis to 0,60 unless the plots seem to have very low values        
        if means[0,len(meshingList)-1] > 40:  
            plt.ylim([0,60])
    
        plt.ylabel('Percentage of pages freed')
        plt.xlabel('Number of objects per page')
            
        labels = [bundle[0] for bundle in meshingList] + [id for id in boundList]
        colors = ["blue", "green", "red", "cyan"]
        patches = []
        for i in range(len(meshingList)+len(boundList)):
            patches.append(mpatches.Patch(color = colors[i], label = labels[i]))
        plt.legend(handles = patches)
        plt.title(title)
        
    else:
        #add your own plot formatting code here
        pass
    
    if save:
        plt.savefig(plotname, dpi = 1000)
    else:
        plt.show()
    plt.close()

    if time:
        for i in range(len(meshingList)):
            plt.errorbar(np.asarray(x), np.asarray(tmeans[:,i]), np.asarray(tstd_devs[:,i]), markersize=3, lw=1, fmt='-o')
            
        if not custom:
            plt.ylabel('Average runtime (ms)')
            plt.xlabel('Number of objects per page')
                
            labels = [bundle[0] for bundle in meshingList]
            colors = ["blue", "green", "red", "cyan"]
            patches = []
            for i in range(len(meshingList)):
                patches.append(mpatches.Patch(color = colors[i], label = labels[i]))
            plt.legend(handles = patches)
            plt.title(title)
        else:
            #add your own plot formatting code here
            pass
        if save:
            time_plotname = filenamer(meshingList, length, numStrings, attempts, boundList, True)
            plt.savefig(time_plotname, dpi = 1000)
        else:
            plt.show()
        plt.close()
    
    dumpToFile(means, std_devs, tmeans, tstd_devs, bound_results, plotname)
                   

#to do: implement ability to read in and mesh string sets instead of randomly generating them
if __name__ == '__main__':
    stringSet = "random"
    stdOutput = True
    
    length = 128
    ones_range_min = 1
    ones_range_max = int(length/2)
    increment = 4
    numStrings = 1000
    attempts = 100
    reps = 10
    
#    length = 16
#    ones_range_min = 1
#    ones_range_max = int(length/2)
#    increment = 1
#    numStrings = 100
#    attempts = 50
#    reps = 10
    
#    length = 256
#    ones_range_min = 40
#    ones_range_max = 59
#    increment = 4
#    numStrings = 10000
#    attempts = 100
#    reps = 4
    
#    meshingList = [("split", {'attempts' : attempts, 'stdOutput' : stdOutput}),
##                   ("dumb", {'attempts' : attempts, 'stdOutput' : stdOutput}),
#                   ("greedysplit", {'stdOutput' : stdOutput, 'cutoff' : (10**(-2))}),
#                   ("greedysplit", {'stdOutput' : stdOutput}), 
#                   ("maxmatch", {'stdOutput' : stdOutput})
#                   ]
    
    meshingList = [("greedysplit", {'stdOutput' : stdOutput, 'cutoff' : (10**(-3))}),    
                   ("greedy", {'stdOutput' : stdOutput, 'cutoff' : (10**(-3))})#,
#                   ("greedysplit", {'stdOutput' : stdOutput}),
#                    ('greedy', {'stdOutput' : stdOutput})
                   ]
    
#    meshingList = [("greedysplit", {'stdOutput' : stdOutput, 'cutoff' : 10**(-5)}),
#                   ("greedysplit", {'stdOutput' : stdOutput})
#                   ]
    
    boundList = []
#    boundList = ["impdeg+1"]
    
    save = False
    time = True             
    title = "Meshing {} {}-bit {} occupancy strings. {} attempts".format(numStrings, length, stringSet, attempts)
    
    x = range(ones_range_min, ones_range_max, increment)
       
    means, std_devs, tmeans, tstd_devs, bound_results = experiment(stringSet = stringSet, length = length, numStrings = numStrings, x = x, meshingList = meshingList, boundList = boundList, reps = reps, time = time)    
    plotIt(means, std_devs, tmeans, tstd_devs, bound_results, stringSet = stringSet, length = length, numStrings = numStrings, x = x, attempts = attempts, meshingList = meshingList, boundList = boundList, reps = reps, title = title, save = save, time = time)
    