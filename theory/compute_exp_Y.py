# -*- coding: utf-8 -*-
"""
Created on Wed Apr 27 22:18:12 2016

@author: devd
"""
from __future__ import division
from choose import compute_q, nCr, compute_p3

def prob(a,b,c, numStrings, numOnes, p1, p2, p3):
    p4 = 1-p1-p2-p3
    d = numStrings-2-a-b-c
    return (nCr(numStrings-2,a)*(p1**a)*nCr(numStrings-2-a, b)*(p2**b)*nCr(numStrings-2-a-b, c)*(p3**c)*(p4**d))

def compute_exp_Y(length, numOnes, numStrings):
    q = compute_q(length, numOnes)
    p3 = compute_p3(length, numOnes)
    p1 = q-p3
    p2 = p1
    
    p4 = 1-p1-p2-p3
    
    sum = 0
    for a in range(numStrings-2+1):
        for b in range(numStrings-2-a+1):
            for c in range(numStrings-2-a-b+1):
                add = min(1/(a+c+1), 1/(b+c+1))*prob(a,b,c,numStrings, numOnes,p1,p2,p3)
#                add = min(1/(a+c), 1/(b+c))*prob(a,b,c,numStrings, numOnes,p1,p2,p3)
                sum += add
#                sum += min(1/(a+c+1), 1/(b+c+1))*prob(a,b,c,numStrings,p1,p2,p3)
    sum *= q
    return sum*nCr(numStrings,2)
    
def compute_degree_bound(length, numOnes, numStrings):
    print length
    print numOnes
    q = compute_q(length, numOnes)
    exp_degree = (numStrings-1)*q
    a = exp_degree
    b = exp_degree
    return numStrings/2*(a/(b+1))
    
def compute_isolated_edge_bound(length, numOnes, numStrings):
    q = compute_q(length, numOnes)
    p3 = compute_p3(length, numOnes)
    m = numStrings
    bound1 = (m-1)*q*(1-(2*q)+p3)**(m-2)
    bound2 = 2- 2*(1-q)**(m-1) - (m-1)*q
    return (m/2)*max(bound1,bound2)
#    return (m/2)*bound2
    
def compute_degreeplusone_bound(length, numOnes, numStrings):
    q = compute_q(length, numOnes)
    p3 = compute_p3(length, numOnes)
    p1 = q-p3
    p2 = p1
    
    p4 = 1-p1-p2-p3
    
    sum = 0
    for a in range(numStrings-2+1):
        for b in range(numStrings-2-a+1):
            for c in range(numStrings-2-a-b+1):
                add = .5*(1/(a+c+2) + 1/(b+c+2))*prob(a,b,c,numStrings, numOnes,p1,p2,p3)
                sum += add
    sum *= q
    return sum*nCr(numStrings,2)
    
def compute_improved_degreeplusone_bound(length, numOnes, numStrings):
    q = compute_q(length, numOnes)
    p3 = compute_p3(length, numOnes)
    p1 = q-p3
    p2 = p1
    
    p4 = 1-p1-p2-p3
    
    sum = 0
    for a in range(numStrings-2+1):
        for b in range(numStrings-2-a+1):
            for c in range(numStrings-2-a-b+1):
                if a+c+1==1 and b+c+1==1:
                    add = 1*prob(a,b,c,numStrings, numOnes,p1,p2,p3)
                elif a+c+1==1 or b+c+1==1:
                    add = .5*prob(a,b,c,numStrings, numOnes,p1,p2,p3)
#                elif a+c+1==2 and b+c+1==2:
#                    add = .5*prob(a,b,c,numStrings, numOnes,p1,p2,p3)
#                elif (a+c+1==2 and b+c+1==3) or (a+c+1==3 and b+c+1==2): 
#                    add = .33*prob(a,b,c,numStrings, numOnes,p1,p2,p3)
                else:
                    add = .5*(1/(a+c+2) + 1/(b+c+2))*prob(a,b,c,numStrings, numOnes,p1,p2,p3)
                sum += add
    sum *= q
    return sum*nCr(numStrings,2)

if __name__ == '__main__':
#print prob(18,2,12,80,0.03125,0.03125,0.9375)/31
    print compute_exp_Y(32, 13, 80)