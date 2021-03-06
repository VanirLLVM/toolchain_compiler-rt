// RUN: %clangxx_msan -m64 -O0 -g %s -o %t && %t
// RUN: %clangxx_msan -m64 -O0 -g -DUNINIT %s -o %t && not %t 2>&1 | FileCheck %s

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

int main(void) {
  unsigned seed;
#ifndef UNINIT
  seed = 42;
#endif
  int v = rand_r(&seed);
  // CHECK: MemorySanitizer: use-of-uninitialized-value
  // CHECK: in main{{.*}}rand_r.cc:[[@LINE-2]]
  if (v) printf(".\n");
  return 0;
}
