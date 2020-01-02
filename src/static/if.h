// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil -*-

/**
 * @file   staticif.h
 * @brief  Statically returns a VALUE based on a conditional.
 * @author Emery Berger <http://www.cs.umass.edu/~emery>
 * @note   Copyright (C) 2005 by Emery Berger, University of Massachusetts Amherst.
 */

#pragma once
#ifndef MESH_STATIC__IF_H
#define MESH_STATIC__IF_H

template <class TYPE>
TYPE constexpr staticif(bool v, TYPE a, TYPE b) {
  return (v ? a : b);
}

#else

template <bool b, int a, int c>
class StaticIf;

template <int a, int b>
class StaticIf<true, a, b> {
public:
  enum { VALUE = a };
};

template <int a, int b>
class StaticIf<false, a, b> {
public:
  enum { VALUE = b };
};

#endif
