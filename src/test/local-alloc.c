#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SZ 61

volatile char *volatile var;

int main() {
  for (size_t i = 0; i < 200000000; i++) {
    var = malloc(SZ);
    /* memset((char *)var, 0, SZ); */
    free((void *)var);
    var = NULL;
  }
}
