#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SZ 32816
#define NOBJS 40

volatile char *volatile var;

int main() {
  char *objects[NOBJS];
  memset(objects, 0, sizeof(*objects) * NOBJS);

  for (size_t i = 0; i < 1000000; i++) {
    var = malloc(SZ);
    memset((char *)var, 0x55, SZ);
    size_t off = i % NOBJS;
    if (objects[off] != NULL)
      free(objects[off]);
    objects[off] = (char *)var;
    var = NULL;
  }

  for (size_t i = 0; i < NOBJS; i++) {
    if (objects[i] != NULL)
      free(objects[i]);
  }
}
