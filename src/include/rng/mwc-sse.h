// -*- C++ -*-

#ifndef MWC_SSE
#define MWC_SSE

#ifndef __SSE4_1__
#error "This header file only works on architectures that support SSE 4.1 or higher (try -msse4.1)."
#endif

#include <smmintrin.h>

/**
 * A faster version of MWC that computes four RNGs in parallel using
 * SIMD operations (SSE).
 *
 *  @author Emery Berger <emery@cs.umass.edu>
 *
 **/

class mwc_sse {
public:
  mwc_sse(unsigned int seed1, unsigned int seed2) {
    init(seed1, seed2);
  }

  inline unsigned int next() {
    unsigned int v = ((unsigned int *)&_xi)[_index];
    _index++;
    if (_index == 4) {
      MWC();
    }
    return v;
  }

private:
  __m128i _zi, _wi, _xi;
  __m128i _mask, _mult, _mult2;
  int _index;

  void init(unsigned int seed1, unsigned int seed2) {
    unsigned int maskvals[] = {65535, 65535, 65535, 65535};
    unsigned int multvals[] = {36969, 36969, 36969, 36969};
    unsigned int multvals2[] = {18000, 18000, 18000, 18000};

    _mask = _mm_load_si128((__m128i *)maskvals);
    _mult = _mm_load_si128((__m128i *)multvals);
    _mult2 = _mm_load_si128((__m128i *)multvals2);

    unsigned int z[4];
    unsigned int w[4];
    for (int i = 0; i < 4; i++) {
      z[i] = seed1 + i;
      w[i] = seed2 + i;
    }
    _zi = _mm_load_si128((__m128i *)z);
    _wi = _mm_load_si128((__m128i *)w);
    MWC();
  }

  void MWC() {
    //      unsigned int zz1 = z[i] & 65535;
    __m128i zi1 = _mm_and_si128(_zi, _mask);

    // unsigned int zz2 = z[i] >> 16;
    __m128i zi2 = _mm_srli_epi32(_zi, 16);

    // multiply and add
    // z[i] = 36969*zz1+zz2;

    __m128i zi3 = _mm_mullo_epi32(_mult, zi1);
    _zi = _mm_add_epi32(zi3, zi2);

    //    unsigned int ww1 = w[i] & 65535;
    __m128i wi1 = _mm_and_si128(_wi, _mask);

    // unsigned int ww2 = w[i] >> 16;
    __m128i wi2 = _mm_srli_epi32(_wi, 16);

    // w[i] = 18000*ww1+ww2;
    __m128i ri1 = _mm_mullo_epi32(_mult2, wi1);
    _wi = _mm_add_epi32(ri1, wi2);

    // x[i] = (z[i] << 16) + w[i];
    _xi = _mm_slli_epi32(_zi, 16);
    _xi = _mm_add_epi32(_xi, _wi);

#if 0
    for (int i = 0; i < 4; i++) {
      cout << "zi[" << i << "] = " << ((unsigned int *) &_zi)[i] << endl;
    }
    for (int i = 0; i < 4; i++) {
      cout << "wi[" << i << "] = " << ((unsigned int *) &_wi)[i] << endl;
    }
    for (int i = 0; i < 4; i++) {
      cout << "xi[" << i << "] = " << ((unsigned int *) &_xi)[i] << endl;
    }
#endif
    _index = 0;
  }
};

#endif
