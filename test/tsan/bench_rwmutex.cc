// RUN: %clangxx_tsan %s -o %t
// RUN: %t 2>&1 | FileCheck %s

#include "bench.h"

pthread_rwlock_t mtx;

void thread(int tid) {
  for (int i = 0; i < bench_niter; i++) {
    pthread_rwlock_rdlock(&mtx);
    pthread_rwlock_unlock(&mtx);
  }
}

void bench() {
  pthread_rwlock_init(&mtx, 0);
  pthread_rwlock_wrlock(&mtx);
  pthread_rwlock_unlock(&mtx);
  pthread_rwlock_rdlock(&mtx);
  pthread_rwlock_unlock(&mtx);
  start_thread_group(bench_nthread, thread);
}

// CHECK: DONE

