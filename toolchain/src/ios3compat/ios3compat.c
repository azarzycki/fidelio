/* libios3compat: implementations of functions iPhone OS 3.x libSystem lacks. */
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <mach/mach.h>
#include <mach/mach_time.h>
#include <mach/thread_act.h>
#include "ios3compat.h"

#ifdef IOS3COMPAT_CLOCK_GETTIME
static uint64_t mach_to_ns(uint64_t t) {
    static mach_timebase_info_data_t tb;
    if (tb.denom == 0) mach_timebase_info(&tb);
    return (t / tb.denom) * tb.numer + (t % tb.denom) * tb.numer / tb.denom;
}

int clock_gettime(clockid_t clk, struct timespec *ts) {
    if (!ts) { errno = EFAULT; return -1; }
    switch (clk) {
    case CLOCK_REALTIME: {
        struct timeval tv;
        if (gettimeofday(&tv, NULL)) return -1;
        ts->tv_sec = tv.tv_sec; ts->tv_nsec = tv.tv_usec * 1000;
        return 0;
    }
    /* mach_absolute_time stops while the device sleeps, i.e. it is CLOCK_UPTIME_RAW;
       it is the best monotonic source on iPhone OS 3 and is used for all monotonic ids. */
    case CLOCK_MONOTONIC: case CLOCK_MONOTONIC_RAW: case CLOCK_MONOTONIC_RAW_APPROX:
    case CLOCK_UPTIME_RAW: case CLOCK_UPTIME_RAW_APPROX: {
        uint64_t ns = mach_to_ns(mach_absolute_time());
        ts->tv_sec = (time_t)(ns / 1000000000ull); ts->tv_nsec = (long)(ns % 1000000000ull);
        return 0;
    }
    case CLOCK_PROCESS_CPUTIME_ID: {
        struct rusage ru;
        if (getrusage(RUSAGE_SELF, &ru)) return -1;
        uint64_t us = (uint64_t)(ru.ru_utime.tv_sec + ru.ru_stime.tv_sec) * 1000000ull + ru.ru_utime.tv_usec + ru.ru_stime.tv_usec;
        ts->tv_sec = (time_t)(us / 1000000); ts->tv_nsec = (long)(us % 1000000) * 1000;
        return 0;
    }
    case CLOCK_THREAD_CPUTIME_ID: {
        thread_basic_info_data_t info; mach_msg_type_number_t cnt = THREAD_BASIC_INFO_COUNT;
        mach_port_t th = mach_thread_self();
        kern_return_t kr = thread_info(th, THREAD_BASIC_INFO, (thread_info_t)&info, &cnt);
        mach_port_deallocate(mach_task_self(), th);
        if (kr != KERN_SUCCESS) { errno = EINVAL; return -1; }
        uint64_t us = (uint64_t)(info.user_time.seconds + info.system_time.seconds) * 1000000ull + info.user_time.microseconds + info.system_time.microseconds;
        ts->tv_sec = (time_t)(us / 1000000); ts->tv_nsec = (long)(us % 1000000) * 1000;
        return 0;
    }
    default:
        errno = EINVAL; return -1;
    }
}

int clock_getres(clockid_t clk, struct timespec *ts) {
    if (clk == CLOCK_REALTIME || clk == CLOCK_PROCESS_CPUTIME_ID || clk == CLOCK_THREAD_CPUTIME_ID) {
        if (ts) { ts->tv_sec = 0; ts->tv_nsec = 1000; }
        return 0;
    }
    if (ts) { uint64_t ns = mach_to_ns(1); ts->tv_sec = 0; ts->tv_nsec = ns ? (long)ns : 1; }
    return 0;
}
#endif

/* Entropy: /dev/urandom (the kernel Yarrow PRNG). */
static int urandom_read(void *buf, size_t n) {
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd < 0) return -1;
    unsigned char *p = buf;
    while (n) {
        ssize_t r = read(fd, p, n);
        if (r < 0) { if (errno == EINTR) continue; close(fd); return -1; }
        if (r == 0) { close(fd); errno = EIO; return -1; }
        p += r; n -= (size_t)r;
    }
    close(fd);
    return 0;
}

int getentropy(void *buf, size_t len) {
    if (len > 256) { errno = EIO; return -1; }
    return urandom_read(buf, len);
}

void arc4random_buf(void *buf, size_t n) {
    if (urandom_read(buf, n) == 0) return;
    /* fall back to libSystem's arc4random (which itself seeds from /dev/urandom) */
    unsigned char *p = buf;
    while (n) {
        uint32_t r = arc4random();
        size_t k = n < 4 ? n : 4;
        memcpy(p, &r, k); p += k; n -= k;
    }
}

uint32_t arc4random_uniform(uint32_t upper) {
    if (upper < 2) return 0;
    uint32_t min = (uint32_t)(-upper) % upper, r;
    do r = arc4random(); while (r < min);
    return r % upper;
}

size_t strnlen(const char *s, size_t maxlen) {
    size_t i = 0;
    while (i < maxlen && s[i]) i++;
    return i;
}

char *strndup(const char *s, size_t n) {
    size_t len = strnlen(s, n);
    char *d = malloc(len + 1);
    if (!d) return NULL;
    memcpy(d, s, len); d[len] = 0;
    return d;
}

void *memmem(const void *big, size_t blen, const void *little, size_t llen) {
    const unsigned char *b = big, *l = little;
    if (llen == 0) return (void *)big;
    if (blen < llen) return NULL;
    for (size_t i = 0; i <= blen - llen; i++)
        if (b[i] == l[0] && memcmp(b + i, l, llen) == 0) return (void *)(b + i);
    return NULL;
}

ssize_t getdelim(char **lp, size_t *n, int delim, FILE *f) {
    if (!lp || !n || !f) { errno = EINVAL; return -1; }
    size_t len = 0;
    int c;
    if (!*lp || *n == 0) { *n = 128; *lp = malloc(*n); if (!*lp) return -1; }
    while ((c = getc(f)) != EOF) {
        if (len + 2 > *n) {
            size_t nn = *n * 2; char *np = realloc(*lp, nn);
            if (!np) return -1;
            *lp = np; *n = nn;
        }
        (*lp)[len++] = (char)c;
        if (c == delim) break;
    }
    if (len == 0) return -1;
    (*lp)[len] = 0;
    return (ssize_t)len;
}

ssize_t getline(char **lp, size_t *n, FILE *f) { return getdelim(lp, n, '\n', f); }

int vdprintf(int fd, const char *fmt, va_list ap) {
    char *s = NULL;
    int len = vasprintf(&s, fmt, ap);
    if (len < 0) return -1;
    ssize_t off = 0;
    while (off < len) {
        ssize_t w = write(fd, s + off, (size_t)(len - off));
        if (w < 0) { if (errno == EINTR) continue; free(s); return -1; }
        off += w;
    }
    free(s);
    return len;
}

int dprintf(int fd, const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    int r = vdprintf(fd, fmt, ap);
    va_end(ap);
    return r;
}
