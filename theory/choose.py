# -*- coding: utf-8 -*-
"""
Created on Wed Apr 27 16:52:27 2016

@author: devd
"""
#from __future__ import division
from math import factorial
from scipy.misc import comb


#def nCr(n, r):
#    return factorial(n) // factorial(n-r) // factorial(r)

#def nCr(n,r):
#    result = factorial(n)
#    result /= factorial(r)
#    result /= factorial(n-r)
#    return result

def nCr(n,r):
    return comb(n,r)




def compute_q(length, numOnes):
    result = float((nCr(length-numOnes, numOnes)))/(nCr(length, numOnes))
    return result
    
def compute_p3(length, numOnes):
    result = float((nCr(length-(2*numOnes), numOnes)))/(nCr(length, numOnes))
    return result
    
#print compute_q(32,1)


if __name__ == '__main__':
    for i in range(9,16):
        print 'q val for {}: {}\n'.format(i,compute_q(32,i))    
    #print compute_q(32,13)
if __name__ == "__main__":    
    print compute_q(32, 6)