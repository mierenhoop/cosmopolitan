/*-*- mode:c;indent-tabs-mode:nil;c-basic-offset:2;tab-width:8;coding:utf-8 -*-│
│vi: set net ft=c ts=2 sts=2 sw=2 fenc=utf-8                                :vi│
╞══════════════════════════════════════════════════════════════════════════════╡
│ Copyright 2022 Justine Alexandra Roberts Tunney                              │
│                                                                              │
│ Permission to use, copy, modify, and/or distribute this software for         │
│ any purpose with or without fee is hereby granted, provided that the         │
│ above copyright notice and this permission notice appear in all copies.      │
│                                                                              │
│ THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL                │
│ WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED                │
│ WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE             │
│ AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL         │
│ DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR        │
│ PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER               │
│ TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR             │
│ PERFORMANCE OF THIS SOFTWARE.                                                │
╚─────────────────────────────────────────────────────────────────────────────*/
#include "libc/calls/calls.h"
#include "libc/calls/struct/timespec.h"
#include "libc/intrin/atomic.h"
#include "libc/intrin/pthread.h"
#include "libc/mem/mem.h"
#include "libc/runtime/gc.internal.h"
#include "libc/testlib/ezbench.h"
#include "libc/testlib/testlib.h"
#include "libc/thread/posixthread.internal.h"

int THREADS = 16;
int ITERATIONS = 100;

int count;
_Atomic(int) started;
_Atomic(int) finished;
pthread_mutex_t lock;
pthread_mutexattr_t attr;

FIXTURE(pthread_mutex_lock, normal) {
  ASSERT_EQ(0, pthread_mutexattr_init(&attr));
  ASSERT_EQ(0, pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_NORMAL));
  ASSERT_EQ(0, pthread_mutex_init(&lock, &attr));
  ASSERT_EQ(0, pthread_mutexattr_destroy(&attr));
}

FIXTURE(pthread_mutex_lock, recursive) {
  ASSERT_EQ(0, pthread_mutexattr_init(&attr));
  ASSERT_EQ(0, pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE));
  ASSERT_EQ(0, pthread_mutex_init(&lock, &attr));
  ASSERT_EQ(0, pthread_mutexattr_destroy(&attr));
}

FIXTURE(pthread_mutex_lock, errorcheck) {
  ASSERT_EQ(0, pthread_mutexattr_init(&attr));
  ASSERT_EQ(0, pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_ERRORCHECK));
  ASSERT_EQ(0, pthread_mutex_init(&lock, &attr));
  ASSERT_EQ(0, pthread_mutexattr_destroy(&attr));
}

////////////////////////////////////////////////////////////////////////////////
// TESTS

int MutexWorker(void *p, int tid) {
  int i;
  ++started;
  for (i = 0; i < ITERATIONS; ++i) {
    pthread_mutex_lock(&lock);
    ++count;
    sched_yield();
    pthread_mutex_unlock(&lock);
  }
  ++finished;
  return 0;
}

TEST(pthread_mutex_lock, contention) {
  int i;
  struct spawn *th = gc(malloc(sizeof(struct spawn) * THREADS));
  pthread_mutexattr_init(&attr);
  pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_NORMAL);
  pthread_mutex_init(&lock, &attr);
  pthread_mutexattr_destroy(&attr);
  count = 0;
  started = 0;
  finished = 0;
  for (i = 0; i < THREADS; ++i) {
    ASSERT_SYS(0, 0, _spawn(MutexWorker, (void *)(intptr_t)i, th + i));
  }
  for (i = 0; i < THREADS; ++i) {
    ASSERT_SYS(0, 0, _join(th + i));
  }
  EXPECT_EQ(THREADS, started);
  EXPECT_EQ(THREADS, finished);
  EXPECT_EQ(THREADS * ITERATIONS, count);
  EXPECT_EQ(0, pthread_mutex_destroy(&lock));
}

////////////////////////////////////////////////////////////////////////////////
// BENCHMARKS

void BenchSpinUnspin(pthread_spinlock_t *s) {
  pthread_spin_lock(s);
  pthread_spin_unlock(s);
}

void BenchLockUnlock(pthread_mutex_t *m) {
  pthread_mutex_lock(m);
  pthread_mutex_unlock(m);
}

BENCH(pthread_mutex_lock, bench_uncontended) {
  {
    pthread_spinlock_t s = {0};
    EZBENCH2("spin 1x", donothing, BenchSpinUnspin(&s));
  }
  {
    pthread_mutex_t m = {PTHREAD_MUTEX_NORMAL};
    EZBENCH2("normal 1x", donothing, BenchLockUnlock(&m));
  }
  {
    pthread_mutex_t m = {PTHREAD_MUTEX_RECURSIVE};
    EZBENCH2("recursive 1x", donothing, BenchLockUnlock(&m));
  }
  {
    pthread_mutex_t m = {PTHREAD_MUTEX_ERRORCHECK};
    EZBENCH2("errorcheck 1x", donothing, BenchLockUnlock(&m));
  }
}

struct SpinContentionArgs {
  pthread_spinlock_t *spin;
  _Atomic(char) done;
  _Atomic(char) ready;
};

int SpinContentionWorker(void *arg, int tid) {
  struct SpinContentionArgs *a = arg;
  while (!atomic_load_explicit(&a->done, memory_order_relaxed)) {
    pthread_spin_lock(a->spin);
    atomic_store_explicit(&a->ready, 1, memory_order_relaxed);
    pthread_spin_unlock(a->spin);
  }
  return 0;
}

struct MutexContentionArgs {
  pthread_mutex_t *mutex;
  _Atomic(char) done;
  _Atomic(char) ready;
};

int MutexContentionWorker(void *arg, int tid) {
  struct MutexContentionArgs *a = arg;
  while (!atomic_load_explicit(&a->done, memory_order_relaxed)) {
    pthread_mutex_lock(a->mutex);
    atomic_store_explicit(&a->ready, 1, memory_order_relaxed);
    pthread_mutex_unlock(a->mutex);
  }
  return 0;
}

BENCH(pthread_mutex_lock, bench_contended) {
  struct spawn t;
  {
    pthread_spinlock_t s = {0};
    struct SpinContentionArgs a = {&s};
    _spawn(SpinContentionWorker, &a, &t);
    while (!a.ready) sched_yield();
    EZBENCH2("spin 2x", donothing, BenchSpinUnspin(&s));
    a.done = true;
    _join(&t);
  }
  {
    pthread_mutex_t m = {PTHREAD_MUTEX_NORMAL};
    struct MutexContentionArgs a = {&m};
    _spawn(MutexContentionWorker, &a, &t);
    while (!a.ready) sched_yield();
    EZBENCH2("normal 2x", donothing, BenchLockUnlock(&m));
    a.done = true;
    _join(&t);
  }
  {
    pthread_mutex_t m = {PTHREAD_MUTEX_RECURSIVE};
    struct MutexContentionArgs a = {&m};
    _spawn(MutexContentionWorker, &a, &t);
    while (!a.ready) sched_yield();
    EZBENCH2("recursive 2x", donothing, BenchLockUnlock(&m));
    a.done = true;
    _join(&t);
  }
  {
    pthread_mutex_t m = {PTHREAD_MUTEX_ERRORCHECK};
    struct MutexContentionArgs a = {&m};
    _spawn(MutexContentionWorker, &a, &t);
    while (!a.ready) sched_yield();
    EZBENCH2("errorcheck 2x", donothing, BenchLockUnlock(&m));
    a.done = true;
    _join(&t);
  }
}