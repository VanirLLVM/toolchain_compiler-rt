// RUN: %clangxx_tsan %s -o %t
// RUN: %t 2>&1 | FileCheck %s

#include "bench.h"

int x;

void thread(int tid) {
  for (int i = 0; i < bench_niter; i++)
    __atomic_fetch_add(&x, 1, __ATOMIC_ACQ_REL);
}

void bench() {
  start_thread_group(bench_nthread, thread);
}

// CHECK: DONE

