#ifndef WGR_TEST_OS_H
#define WGR_TEST_OS_H

/* The little the tests need from the OS that C doesn't give them: a pause and a
 * monotonic clock, for Windows (MSVC or MinGW) and POSIX alike. Threads are
 * wgrender's own (internal/wgr_thread_internal.h). */

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

static inline void test_sleep_ms(int ms) { Sleep((DWORD)ms); }

static inline double test_now_seconds(void)
{
    LARGE_INTEGER frequency, counter;
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&counter);
    return (double)counter.QuadPart / (double)frequency.QuadPart;
}
#else
#include <time.h>

static inline void test_sleep_ms(int ms)
{
    const struct timespec pause = {ms / 1000, (long)(ms % 1000) * 1000000L};
    nanosleep(&pause, NULL);
}

static inline double test_now_seconds(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}
#endif

#endif // WGR_TEST_OS_H
