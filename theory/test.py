# -*- coding: utf-8 -*-
"""
Created on Mon Apr 25 14:34:04 2016

@author: devd
"""
from __future__ import division
import math
from choose import nCr
import numpy as np
from scipy.misc import comb
import createRandomString as c

q = .5
numOnes = 5
print c.createIndependentRandomStrings(10, 2, q = q, numOnes = numOnes)