// RUN: %clangxx_tsan %s -o %t
// RUN: %t 2>&1 | FileCheck %s

#include "bench.h"

pthread_mutex_t mtx;
pthread_cond_t cv;
int x;

void thread(int tid) {
  for (int i = 0; i < bench_niter; i++) {
    pthread_mutex_lock(&mtx);
    while (x != i * 2 + tid)
      pthread_cond_wait(&cv, &mtx);
    x++;
    pthread_cond_signal(&cv);
    pthread_mutex_unlock(&mtx);
  }
}

void bench() {
  pthread_mutex_init(&mtx, 0);
  pthread_cond_init(&cv, 0);
  start_thread_group(2, thread);
}

// CHECK: DONE

