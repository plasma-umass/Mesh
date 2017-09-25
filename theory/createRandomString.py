# -*- coding: utf-8 -*-
"""
Created on Sat Feb 27 21:20:37 2016

@author: David Tench
"""
import random
import math

def createRandomString(length, numOnes):
    """Returns a random binary string with specified number of randomly located
    ones."""
    counter = numOnes
    string = ''
    for i in range(length):
        string += '0'
    while counter !=0:
        loc = random.randrange(length)
        while string[loc] == '1':
            loc = random.randrange(length)
        string = string[:loc] + '1' + string[loc+1:]
        counter -= 1
    return string

def createConstRandomStrings(length, numStrings, numOnes):
    """Returns a list of random binary strings, each with exactly numOnes ones."""
    strings = []
    for i in range(numStrings):
        strings.append(createRandomString(length, numOnes))
    return strings
    
def createIndependentRandomStrings(length, numStrings, q = -1, numOnes = -1):
    """Returns a set (size numStrings) of binary strings where each bit is
    randomly 0 or 1 with probability s.t. the probability of 2 strings meshing
    is q."""
    if q >= 0:
#        print 'q = (1-p^2)^b'
        p = math.sqrt(1 - (q**(1.0/length)))
    elif numOnes > 0:
#        print 'p = n/b'
        p = float(numOnes)/length
#        print "q = {}".format(((1 - (p)**2)**length))
    elif numOnes == 0:
        raise Exception("numOnes should not be 0.")
    else: 
         raise Exception('must specify q or numOnes')   
#    print "q = {}".format(q)
#    print "p = {}".format(p)
#    print "occupancy mean is {} variance is {}".format(length*p,length*p*(1-p))
    all_strings = []
    for i in range(numStrings):
        string = ''
        for i in range(length):
            if random.random() < p:
                string += '1'
            else:
                string += '0'
        all_strings.append(string)
        string = ''            
        
    return all_strings

def providedStrings(filename):
    """not yet implemented.  will return the list of strings from some location."""
    return None
    

#print createRandomString(32, 16)
#print createIndependentRandomStrings(32,.5,1)