#ifndef WGRI_INTERNAL_THREAD_H
#define WGRI_INTERNAL_THREAD_H

#include <stdbool.h>

/* Minimal threads for library internals (the asset pipeline's workers): POSIX
 * threads, or Win32 on Windows. On web builds without -pthread there are no
 * threads: wgri_thread_available() is false and wgri_thread_create fails. */

#if defined(_WIN32)
typedef struct { void *handle; } wgri_thread_t;
typedef struct { void *opaque[5]; } wgri_mutex_t;   /* CRITICAL_SECTION-sized, see wgr_thread.c */
typedef struct { void *opaque; } wgri_cond_t;       /* CONDITION_VARIABLE */
#else
#include <pthread.h>
typedef struct { pthread_t handle; } wgri_thread_t;
typedef struct { pthread_mutex_t handle; } wgri_mutex_t;
typedef struct { pthread_cond_t handle; } wgri_cond_t;
#endif

typedef void (*wgri_thread_fn)(void *arg);

bool wgri_thread_available(void);
/* Logical CPU cores (at least 1). */
int wgri_thread_cpu_count(void);

bool wgri_thread_create(wgri_thread_t *thread, wgri_thread_fn fn, void *arg);
void wgri_thread_join(wgri_thread_t *thread);
/* Let the thread end on its own; its resources are freed when it does. */
void wgri_thread_detach(wgri_thread_t *thread);

void wgri_mutex_init(wgri_mutex_t *mutex);
void wgri_mutex_destroy(wgri_mutex_t *mutex);
void wgri_mutex_lock(wgri_mutex_t *mutex);
void wgri_mutex_unlock(wgri_mutex_t *mutex);

void wgri_cond_init(wgri_cond_t *cond);
void wgri_cond_destroy(wgri_cond_t *cond);
void wgri_cond_wait(wgri_cond_t *cond, wgri_mutex_t *mutex);
void wgri_cond_broadcast(wgri_cond_t *cond);

/* Seconds from an arbitrary start, monotonic. */
double wgri_thread_now(void);

#endif // WGRI_INTERNAL_THREAD_H
