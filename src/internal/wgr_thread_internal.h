#ifndef WGR_INTERNAL_THREAD_H
#define WGR_INTERNAL_THREAD_H

#include <stdbool.h>

/* Minimal threads for library internals (the asset pipeline's workers): POSIX
 * threads, or Win32 on Windows. On web builds without -pthread there are no
 * threads: wgr_thread_available() is false and wgr_thread_create fails. */

#if defined(_WIN32)
typedef struct { void *handle; } wgr_thread_t;
typedef struct { void *opaque[5]; } wgr_mutex_t;   /* CRITICAL_SECTION-sized, see wgr_thread.c */
typedef struct { void *opaque; } wgr_cond_t;       /* CONDITION_VARIABLE */
#else
#include <pthread.h>
typedef struct { pthread_t handle; } wgr_thread_t;
typedef struct { pthread_mutex_t handle; } wgr_mutex_t;
typedef struct { pthread_cond_t handle; } wgr_cond_t;
#endif

typedef void (*wgr_thread_fn)(void *arg);

bool wgr_thread_available(void);
/* Logical CPU cores (at least 1). */
int wgr_thread_cpu_count(void);

bool wgr_thread_create(wgr_thread_t *thread, wgr_thread_fn fn, void *arg);
void wgr_thread_join(wgr_thread_t *thread);
/* Let the thread end on its own; its resources are freed when it does. */
void wgr_thread_detach(wgr_thread_t *thread);

void wgr_mutex_init(wgr_mutex_t *mutex);
void wgr_mutex_destroy(wgr_mutex_t *mutex);
void wgr_mutex_lock(wgr_mutex_t *mutex);
void wgr_mutex_unlock(wgr_mutex_t *mutex);

void wgr_cond_init(wgr_cond_t *cond);
void wgr_cond_destroy(wgr_cond_t *cond);
void wgr_cond_wait(wgr_cond_t *cond, wgr_mutex_t *mutex);
void wgr_cond_broadcast(wgr_cond_t *cond);

/* Seconds from an arbitrary start, monotonic. */
double wgr_thread_now(void);

#endif // WGR_INTERNAL_THREAD_H
