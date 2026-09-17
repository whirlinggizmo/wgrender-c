#ifndef SK_INTERNAL_THREAD_H
#define SK_INTERNAL_THREAD_H

#include <stdbool.h>

/* Minimal threads for library internals (the asset pipeline's workers): POSIX
 * threads, or Win32 on Windows. On web builds without -pthread there are no
 * threads: sk_thread_available() is false and sk_thread_create fails. */

#if defined(_WIN32)
typedef struct { void *handle; } sk_thread_t;
typedef struct { void *opaque[5]; } sk_mutex_t;   /* CRITICAL_SECTION-sized, see sk_thread.c */
typedef struct { void *opaque; } sk_cond_t;       /* CONDITION_VARIABLE */
#else
#include <pthread.h>
typedef struct { pthread_t handle; } sk_thread_t;
typedef struct { pthread_mutex_t handle; } sk_mutex_t;
typedef struct { pthread_cond_t handle; } sk_cond_t;
#endif

typedef void (*sk_thread_fn)(void *arg);

bool sk_thread_available(void);
/* Logical CPU cores (at least 1). */
int sk_thread_cpu_count(void);

bool sk_thread_create(sk_thread_t *thread, sk_thread_fn fn, void *arg);
void sk_thread_join(sk_thread_t *thread);

void sk_mutex_init(sk_mutex_t *mutex);
void sk_mutex_destroy(sk_mutex_t *mutex);
void sk_mutex_lock(sk_mutex_t *mutex);
void sk_mutex_unlock(sk_mutex_t *mutex);

void sk_cond_init(sk_cond_t *cond);
void sk_cond_destroy(sk_cond_t *cond);
void sk_cond_wait(sk_cond_t *cond, sk_mutex_t *mutex);
void sk_cond_broadcast(sk_cond_t *cond);

/* Seconds from an arbitrary start, monotonic. */
double sk_thread_now(void);

#endif // SK_INTERNAL_THREAD_H
