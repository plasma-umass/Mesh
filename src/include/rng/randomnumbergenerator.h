// -*- C++ -*-

/**
 * @file   randomnumbergenerator.h
 * @brief  A generic interface to random number generators.
 * @author Emery Berger <http://www.cs.umass.edu/~emery>
 * @note   Copyright (C) 2005 by Emery Berger, University of Massachusetts Amherst.
 */

#ifndef DH_RANDOMNUMBERGENERATOR_H
#define DH_RANDOMNUMBERGENERATOR_H

#include "mwc.h"
#include "realrandomvalue.h"

class RandomNumberGenerator {
public:

  RandomNumberGenerator()
    : mt (RealRandomValue::value(), RealRandomValue::value())
  {
  }

  inline unsigned int next (void) {
    return mt.next();
  }

private:
  
  MWC mt;

};

#endif
