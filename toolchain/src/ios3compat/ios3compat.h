/* ios3compat.h - POSIX/BSD functions missing from iPhone OS 3.x libSystem.
 * Implemented in libios3compat.a (shipped with the armv6-ios3 toolchain).
 * The armv6-ios3-clang wrapper force-includes this header when IOS3_COMPAT=1 (default). */
#ifndef IOS3COMPAT_H
#define IOS3COMPAT_H
#if defined(__APPLE__) && defined(__arm__)
#include <sys/types.h>
#include <stddef.h>
#include <stdio.h>
#include <stdarg.h>
#include <time.h>
#ifdef __cplusplus
extern "C" {
#endif
#ifndef CLOCK_REALTIME
typedef int clockid_t;
#define CLOCK_REALTIME           0
#define CLOCK_MONOTONIC_RAW      4
#define CLOCK_MONOTONIC_RAW_APPROX 5
#define CLOCK_MONOTONIC          6
#define CLOCK_UPTIME_RAW         8
#define CLOCK_UPTIME_RAW_APPROX  9
#define CLOCK_PROCESS_CPUTIME_ID 12
#define CLOCK_THREAD_CPUTIME_ID  16
int clock_gettime(clockid_t clk, struct timespec *ts);
int clock_getres(clockid_t clk, struct timespec *ts);
#define IOS3COMPAT_CLOCK_GETTIME 1
#endif
void arc4random_buf(void *buf, size_t n);
uint32_t arc4random_uniform(uint32_t upper_bound);
int getentropy(void *buf, size_t len);
size_t strnlen(const char *s, size_t maxlen);
char *strndup(const char *s, size_t n);
void *memmem(const void *big, size_t big_len, const void *little, size_t little_len);
ssize_t getdelim(char **lineptr, size_t *n, int delim, FILE *stream);
ssize_t getline(char **lineptr, size_t *n, FILE *stream);
int dprintf(int fd, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
int vdprintf(int fd, const char *fmt, va_list ap);
#ifndef MSG_NOSIGNAL
/* No MSG_NOSIGNAL on Darwin: set SO_NOSIGPIPE on the socket instead. */
#define MSG_NOSIGNAL 0
#endif
#ifdef __cplusplus
}
#endif
#endif
#endif
