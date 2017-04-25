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

logging.getLogger('').handlers = []
logging.basicConfig(level=logging.DEBUG, format='%(asctime)s - %(levelname)s - %(message)s')
logging.debug('This is a log message.')
logging.info('test')
logging.warning('double test')