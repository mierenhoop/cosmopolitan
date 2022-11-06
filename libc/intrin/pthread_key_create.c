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
#include "libc/errno.h"
#include "libc/intrin/atomic.h"
#include "libc/runtime/runtime.h"
#include "libc/thread/posixthread.internal.h"
#include "libc/thread/thread.h"

/**
 * Allocates TLS slot.
 *
 * If `dtor` is non-null, then it'll be called upon thread exit when the
 * key's value is nonzero. The key's value is set to zero before it gets
 * called. The ordering for multiple destructor calls is unspecified.
 *
 * The result should be passed to pthread_key_delete() later.
 *
 * @param key is set to the allocated key on success
 * @param dtor specifies an optional destructor callback
 * @return 0 on success, or errno on error
 * @raise EAGAIN if `PTHREAD_KEYS_MAX` keys exist
 */
int pthread_key_create(pthread_key_t *key, pthread_key_dtor dtor) {
  int i;
  pthread_key_dtor expect;
  if (!dtor) dtor = (pthread_key_dtor)-1;
  for (i = 0; i < PTHREAD_KEYS_MAX; ++i) {
    if (!(expect = atomic_load_explicit(_pthread_key_dtor + i,
                                        memory_order_relaxed)) &&
        atomic_compare_exchange_strong_explicit(_pthread_key_dtor + i, &expect,
                                                dtor, memory_order_relaxed,
                                                memory_order_relaxed)) {
      *key = i;
      return 0;
    }
  }
  return EAGAIN;
}

__attribute__((__constructor__)) static textstartup void _pthread_key_init() {
  atexit(_pthread_key_destruct);
}
