// -*- C++ -*-

/**
 * @file  realrandomvalue.h
 * @brief Uses a real source of randomness to generate some random values.
 * @author Emery Berger <http://www.cs.umass.edu/~emery>
 * @note   Copyright (C) 2005 by Emery Berger, University of Massachusetts Amherst.
 */


#ifndef DH_REALRANDOMVALUE_H
#define DH_REALRANDOMVALUE_H

#include <stdio.h>
#include <stdlib.h>


#if defined(linux) || defined(__APPLE__)
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#endif

/**
 * @class RealRandomValue
 * @brief Uses a real source of randomness to generate some random values.
 * @author Emery Berger <http://www.cs.umass.edu/~emery>
 */

#if defined(_WIN32)
#include <windows.h>
#include <Wincrypt.h>
#else
#include <sys/time.h>
#include <unistd.h>
#endif

class RealRandomValue {
public:
  RealRandomValue (void)
  {}

  static unsigned int value (void) {
#if defined(_WIN32)

    HCRYPTPROV   hCryptProv;
    BYTE         pbData[16];

    CryptAcquireContext (&hCryptProv, NULL, NULL, PROV_RSA_FULL, 0);
    CryptGenRandom (hCryptProv, 8, pbData);
    unsigned int v = *((unsigned int *) &pbData) | 1;
    return v;
#elif defined(linux) || defined(__APPLE__)
    // Pull random values out of /dev/urandom.
    unsigned int v;

    int fn = open ("/dev/urandom", O_RDONLY);
    if (fn == -1) {
      printf ("FAILED TO OPEN /dev/urandom.\n");
      abort();
    }
    
    if (read(fn, &v, sizeof(unsigned int)) != sizeof(unsigned int)) {
      printf ("FAILED TO READ FROM /dev/urandom.\n");
      abort();
    }

    close (fn);
    return v;
#else
    // Not really random...
    unsigned int v;
    struct timeval tp;
    gettimeofday (&tp, NULL);
    v = getpid() + tp.tv_usec + tp.tv_sec;
    return v;
#endif
  }
};

#endif
