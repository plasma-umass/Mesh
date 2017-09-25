# -*- coding: utf-8 -*-
"""
Created on Mon Apr 25 14:34:04 2016

@author: devd
"""
from __future__ import division
import logging
import math
from choose import nCr
import numpy as np
from scipy.misc import comb
import createRandomString as c
import meshers
import time
import random
import functools
import json
import pickle
import os
from mesh_util import occupancySort, formatStrings, fast_q
from createRandomString import createIndependentRandomStrings

#logging.getLogger('').handlers = []
#logging.basicConfig(level=logging.DEBUG, format='%(asctime)s - %(levelname)s - %(message)s')
#logging.debug('This is a log message.')
#logging.info('test')
#logging.warning('double test')
#

#strings = createIndependentRandomStrings(4,10,numOnes = 2)
#new_strings = []
#for string in strings:
#    new_strings.append((string, long(string, base=2)))
#print new_strings
#print "\n \n \n"
##occupancySort(strings)
#new_strings.sort(key = lambda x: x[0].count("1"))
#print new_strings

strings = createIndependentRandomStrings(256, 10000, numOnes = 5)
strings = formatStrings(strings)
occs = [x[2] for x in strings]
print np.mean(occs)
print np.std(occs)

def faster_q(length, occ1, occ2):
    numerator = 1
    for i in range(length-occ1, length-occ1-occ2, -1):
        numerator *= i
    denominator = 1
    for i in range(length, length-occ2, -1):
        denominator *= i
    return float(numerator)/float(denominator)

length = 128

start = time.time()
for occ1 in range(0,50):
    for occ2 in range(0,50):
        result1 = fast_q(length, occ1, occ2)
t1 = time.time() - start

start = time.time()
for occ1 in range(0,50):
    for occ2 in range(0,50):
        result2 = faster_q(length, occ1, occ2)
t2 = time.time()-start
print 'fast_q got {} in {} ms'.format(result1, t1)
print 'faster_q got {} in {} ms'.format(result2, t2)