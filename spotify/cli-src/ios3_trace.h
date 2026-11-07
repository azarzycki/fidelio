#pragma once
// Flushed trace lines with the process resident size, so the last line in a log
// localizes a failure even when the device reboots (jetsam/watchdog) right after.
#include <stdio.h>
#ifdef __APPLE__
#include <mach/mach.h>
static inline unsigned long ios3_rss_kb() {
  struct task_basic_info info;
  mach_msg_type_number_t count = TASK_BASIC_INFO_COUNT;
  if (task_info(mach_task_self(), TASK_BASIC_INFO, (task_info_t)&info, &count) != KERN_SUCCESS) return 0;
  return (unsigned long)(info.resident_size / 1024);
}
#else  // host test build (Linux)
#include <unistd.h>
static inline unsigned long ios3_rss_kb() {
  long pages = 0, rss = 0;
  FILE* f = fopen("/proc/self/statm", "r");
  if (!f) return 0;
  if (fscanf(f, "%ld %ld", &pages, &rss) != 2) rss = 0;
  fclose(f);
  return (unsigned long)rss * (unsigned long)sysconf(_SC_PAGESIZE) / 1024;
}
#endif

#define IOS3_TRACE(...)                                      \
  do {                                                       \
    fprintf(stdout, "T [rss %luK] ", ios3_rss_kb());          \
    fprintf(stdout, __VA_ARGS__);                            \
    fputc('\n', stdout);                                     \
    fflush(stdout);                                          \
  } while (0)
