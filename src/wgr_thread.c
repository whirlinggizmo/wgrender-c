#include "internal/wgr_thread_internal.h"

#include <stdlib.h>

#if defined(_WIN32)
/* SRW locks and condition variables are Vista's; an older MinGW (Nim's gcc 11) targets
 * XP unless told otherwise, and then declares neither. */
#if !defined(_WIN32_WINNT) || _WIN32_WINNT < 0x0600
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#include <windows.h>
#elif defined(__EMSCRIPTEN__)
#include <emscripten.h>
#include <emscripten/threading.h>
#include <time.h>
#else
#include <time.h>
#include <unistd.h>
#endif

typedef struct {
    wgri_thread_fn fn;
    void *arg;
} start_t;

#if defined(_WIN32)

/* An SRWLOCK, not a CRITICAL_SECTION: one pointer at every width, and not recursive,
 * like the default pthread mutex the other platforms use. */
_Static_assert(sizeof(SRWLOCK) <= sizeof(wgri_mutex_t), "wgri_mutex_t too small");
_Static_assert(sizeof(CONDITION_VARIABLE) <= sizeof(wgri_cond_t), "wgri_cond_t too small");

static DWORD WINAPI run(LPVOID param)
{
    start_t start = *(start_t *)param;
    free(param);
    start.fn(start.arg);
    return 0;
}

bool wgri_thread_available(void) { return true; }

int wgri_thread_cpu_count(void)
{
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    return info.dwNumberOfProcessors > 0 ? (int)info.dwNumberOfProcessors : 1;
}

bool wgri_thread_create(wgri_thread_t *thread, wgri_thread_fn fn, void *arg)
{
    start_t *start = (start_t *)malloc(sizeof(start_t));
    if (start == NULL) return false;
    *start = (start_t){fn, arg};
    thread->handle = CreateThread(NULL, 0, run, start, 0, NULL);
    if (thread->handle == NULL) {
        free(start);
        return false;
    }
    return true;
}

void wgri_thread_join(wgri_thread_t *thread)
{
    WaitForSingleObject((HANDLE)thread->handle, INFINITE);
    CloseHandle((HANDLE)thread->handle);
}

void wgri_thread_detach(wgri_thread_t *thread) { CloseHandle((HANDLE)thread->handle); }

void wgri_mutex_init(wgri_mutex_t *mutex) { InitializeSRWLock((SRWLOCK *)mutex); }
void wgri_mutex_destroy(wgri_mutex_t *mutex) { (void)mutex; }
void wgri_mutex_lock(wgri_mutex_t *mutex) { AcquireSRWLockExclusive((SRWLOCK *)mutex); }
void wgri_mutex_unlock(wgri_mutex_t *mutex) { ReleaseSRWLockExclusive((SRWLOCK *)mutex); }

void wgri_cond_init(wgri_cond_t *cond) { InitializeConditionVariable((CONDITION_VARIABLE *)cond); }
void wgri_cond_destroy(wgri_cond_t *cond) { (void)cond; }
void wgri_cond_wait(wgri_cond_t *cond, wgri_mutex_t *mutex)
{
    SleepConditionVariableSRW((CONDITION_VARIABLE *)cond, (SRWLOCK *)mutex, INFINITE, 0);
}
void wgri_cond_broadcast(wgri_cond_t *cond) { WakeAllConditionVariable((CONDITION_VARIABLE *)cond); }

double wgri_thread_now(void)
{
    static LARGE_INTEGER frequency;
    LARGE_INTEGER counter;
    if (frequency.QuadPart == 0) QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&counter);
    return (double)counter.QuadPart / (double)frequency.QuadPart;
}

#else

static void *run(void *param)
{
    start_t start = *(start_t *)param;
    free(param);
    start.fn(start.arg);
    return NULL;
}

bool wgri_thread_available(void)
{
#if defined(__EMSCRIPTEN__) && !defined(__EMSCRIPTEN_PTHREADS__)
    return false;
#else
    return true;
#endif
}

int wgri_thread_cpu_count(void)
{
#if defined(__EMSCRIPTEN__)
#  if defined(__EMSCRIPTEN_PTHREADS__)
    const int count = emscripten_num_logical_cores();
#  else
    const int count = 1;
#  endif
#elif defined(_WIN32)
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    const long count = (long)info.dwNumberOfProcessors;
#else
    const long count = sysconf(_SC_NPROCESSORS_ONLN);
#endif
    return count > 0 ? (int)count : 1;
}

bool wgri_thread_create(wgri_thread_t *thread, wgri_thread_fn fn, void *arg)
{
    start_t *start;
    if (!wgri_thread_available() || (start = (start_t *)malloc(sizeof(start_t))) == NULL) return false;
    *start = (start_t){fn, arg};
    if (pthread_create(&thread->handle, NULL, run, start) != 0) {
        free(start);
        return false;
    }
    return true;
}

void wgri_thread_join(wgri_thread_t *thread) { pthread_join(thread->handle, NULL); }
void wgri_thread_detach(wgri_thread_t *thread) { pthread_detach(thread->handle); }

void wgri_mutex_init(wgri_mutex_t *mutex) { pthread_mutex_init(&mutex->handle, NULL); }
void wgri_mutex_destroy(wgri_mutex_t *mutex) { pthread_mutex_destroy(&mutex->handle); }
void wgri_mutex_lock(wgri_mutex_t *mutex) { pthread_mutex_lock(&mutex->handle); }
void wgri_mutex_unlock(wgri_mutex_t *mutex) { pthread_mutex_unlock(&mutex->handle); }

void wgri_cond_init(wgri_cond_t *cond) { pthread_cond_init(&cond->handle, NULL); }
void wgri_cond_destroy(wgri_cond_t *cond) { pthread_cond_destroy(&cond->handle); }
void wgri_cond_wait(wgri_cond_t *cond, wgri_mutex_t *mutex) { pthread_cond_wait(&cond->handle, &mutex->handle); }
void wgri_cond_broadcast(wgri_cond_t *cond) { pthread_cond_broadcast(&cond->handle); }

double wgri_thread_now(void)
{
#if defined(__EMSCRIPTEN__)
    return emscripten_get_now() / 1000.0;
#else
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + (double)t.tv_nsec / 1e9;
#endif
}

#endif
