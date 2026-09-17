#include "internal/sk_thread.h"

#include <stdlib.h>

#if defined(_WIN32)
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
    sk_thread_fn fn;
    void *arg;
} start_t;

#if defined(_WIN32)

_Static_assert(sizeof(CRITICAL_SECTION) <= sizeof(sk_mutex_t), "sk_mutex_t too small");
_Static_assert(sizeof(CONDITION_VARIABLE) <= sizeof(sk_cond_t), "sk_cond_t too small");

static DWORD WINAPI run(LPVOID param)
{
    start_t start = *(start_t *)param;
    free(param);
    start.fn(start.arg);
    return 0;
}

bool sk_thread_available(void) { return true; }

int sk_thread_cpu_count(void)
{
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    return info.dwNumberOfProcessors > 0 ? (int)info.dwNumberOfProcessors : 1;
}

bool sk_thread_create(sk_thread_t *thread, sk_thread_fn fn, void *arg)
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

void sk_thread_join(sk_thread_t *thread)
{
    WaitForSingleObject((HANDLE)thread->handle, INFINITE);
    CloseHandle((HANDLE)thread->handle);
}

void sk_mutex_init(sk_mutex_t *mutex) { InitializeCriticalSection((CRITICAL_SECTION *)mutex); }
void sk_mutex_destroy(sk_mutex_t *mutex) { DeleteCriticalSection((CRITICAL_SECTION *)mutex); }
void sk_mutex_lock(sk_mutex_t *mutex) { EnterCriticalSection((CRITICAL_SECTION *)mutex); }
void sk_mutex_unlock(sk_mutex_t *mutex) { LeaveCriticalSection((CRITICAL_SECTION *)mutex); }

void sk_cond_init(sk_cond_t *cond) { InitializeConditionVariable((CONDITION_VARIABLE *)cond); }
void sk_cond_destroy(sk_cond_t *cond) { (void)cond; }
void sk_cond_wait(sk_cond_t *cond, sk_mutex_t *mutex)
{
    SleepConditionVariableCS((CONDITION_VARIABLE *)cond, (CRITICAL_SECTION *)mutex, INFINITE);
}
void sk_cond_broadcast(sk_cond_t *cond) { WakeAllConditionVariable((CONDITION_VARIABLE *)cond); }

double sk_thread_now(void)
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

bool sk_thread_available(void)
{
#if defined(__EMSCRIPTEN__) && !defined(__EMSCRIPTEN_PTHREADS__)
    return false;
#else
    return true;
#endif
}

int sk_thread_cpu_count(void)
{
#if defined(__EMSCRIPTEN__)
#  if defined(__EMSCRIPTEN_PTHREADS__)
    const int count = emscripten_num_logical_cores();
#  else
    const int count = 1;
#  endif
#else
    const long count = sysconf(_SC_NPROCESSORS_ONLN);
#endif
    return count > 0 ? (int)count : 1;
}

bool sk_thread_create(sk_thread_t *thread, sk_thread_fn fn, void *arg)
{
    start_t *start;
    if (!sk_thread_available() || (start = (start_t *)malloc(sizeof(start_t))) == NULL) return false;
    *start = (start_t){fn, arg};
    if (pthread_create(&thread->handle, NULL, run, start) != 0) {
        free(start);
        return false;
    }
    return true;
}

void sk_thread_join(sk_thread_t *thread) { pthread_join(thread->handle, NULL); }

void sk_mutex_init(sk_mutex_t *mutex) { pthread_mutex_init(&mutex->handle, NULL); }
void sk_mutex_destroy(sk_mutex_t *mutex) { pthread_mutex_destroy(&mutex->handle); }
void sk_mutex_lock(sk_mutex_t *mutex) { pthread_mutex_lock(&mutex->handle); }
void sk_mutex_unlock(sk_mutex_t *mutex) { pthread_mutex_unlock(&mutex->handle); }

void sk_cond_init(sk_cond_t *cond) { pthread_cond_init(&cond->handle, NULL); }
void sk_cond_destroy(sk_cond_t *cond) { pthread_cond_destroy(&cond->handle); }
void sk_cond_wait(sk_cond_t *cond, sk_mutex_t *mutex) { pthread_cond_wait(&cond->handle, &mutex->handle); }
void sk_cond_broadcast(sk_cond_t *cond) { pthread_cond_broadcast(&cond->handle); }

double sk_thread_now(void)
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
