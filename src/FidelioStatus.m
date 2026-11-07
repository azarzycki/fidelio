#import "FidelioStatus.h"

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <mach/mach.h>
#include <mach/mach_host.h>
#include <net/if.h>
#include <netinet/in.h>
#include <sys/sysctl.h>
#include <sys/time.h>

@implementation FidelioStatus

@synthesize dict, cover, fileMissing;

- (id)init {
  if ((self = [super init])) {
    coverName = @"";
    [UIDevice currentDevice].batteryMonitoringEnabled = YES;
  }
  return self;
}

- (void)dealloc {
  [dict release];
  [cover release];
  [coverName release];
  [coverMtime release];
  [readTime release];
  [super dealloc];
}

- (void)refresh {
  NSString* path = [FIDELIO_DIR stringByAppendingPathComponent:@"status.plist"];
  NSDictionary* d = [NSDictionary dictionaryWithContentsOfFile:path];
  if (d) {
    [dict release];
    dict = [d retain];
    [readTime release];
    readTime = [[NSDate alloc] init];
    fileMissing = NO;
  } else {
    fileMissing = YES;  // keep the last good dict so the screen does not flicker on a partial read
  }

  // Cover: reload when the name or the modification time changes.
  NSString* name = [self string:@"cover"];
  if ([name length] == 0) {
    if (cover) {
      [cover release];
      cover = nil;
    }
    [coverName release];
    coverName = [@"" retain];
    return;
  }
  NSString* coverPath = [FIDELIO_DIR stringByAppendingPathComponent:name];
  NSDictionary* attrs = [[NSFileManager defaultManager] attributesOfItemAtPath:coverPath error:NULL];
  NSDate* mtime = [attrs objectForKey:NSFileModificationDate];
  BOOL changed = ![name isEqualToString:coverName] || !cover ||
                 (mtime && (!coverMtime || ![mtime isEqualToDate:coverMtime]));
  if (changed) {
    UIImage* img = [[UIImage alloc] initWithContentsOfFile:coverPath];
    if (img) {
      [cover release];
      cover = img;
      [coverName release];
      coverName = [name retain];
      [coverMtime release];
      coverMtime = [mtime retain];
    } else {
      [img release];
    }
  }
}

- (NSString*)string:(NSString*)key {
  id v = [dict objectForKey:key];
  return [v isKindOfClass:[NSString class]] ? v : @"";
}

- (long long)integer:(NSString*)key {
  id v = [dict objectForKey:key];
  return [v respondsToSelector:@selector(longLongValue)] ? [v longLongValue] : 0;
}

- (NSString*)state {
  return dict ? [self string:@"state"] : @"";
}

- (long long)durationMs {
  return [self integer:@"duration_ms"];
}

- (long long)positionMsNow {
  long long pos = [self integer:@"position_ms"];
  if ([[self state] isEqualToString:@"playing"] && readTime) {
    pos += (long long)([[NSDate date] timeIntervalSinceDate:readTime] * 1000.0);
  }
  long long dur = [self durationMs];
  if (dur > 0 && pos > dur) pos = dur;
  return pos;
}

- (NSTimeInterval)fileAge {
  long long updated = [self integer:@"updated"];
  if (!updated) return -1;
  return [[NSDate date] timeIntervalSince1970] - (NSTimeInterval)updated;
}

+ (unsigned long)ownRssKB {
  struct task_basic_info info;
  mach_msg_type_number_t count = TASK_BASIC_INFO_COUNT;
  if (task_info(mach_task_self(), TASK_BASIC_INFO, (task_info_t)&info, &count) != KERN_SUCCESS) return 0;
  return (unsigned long)(info.resident_size / 1024);
}

+ (unsigned long)freeMemoryKB {
  vm_statistics_data_t vm;
  mach_msg_type_number_t count = HOST_VM_INFO_COUNT;
  if (host_statistics(mach_host_self(), HOST_VM_INFO, (host_info_t)&vm, &count) != KERN_SUCCESS) return 0;
  vm_size_t pageSize = 4096;
  host_page_size(mach_host_self(), &pageSize);
  return (unsigned long)(((unsigned long long)vm.free_count * pageSize) / 1024);
}

+ (NSTimeInterval)uptime {
  struct timeval boot;
  size_t len = sizeof boot;
  int mib[2] = {CTL_KERN, KERN_BOOTTIME};
  if (sysctl(mib, 2, &boot, &len, NULL, 0) != 0 || boot.tv_sec == 0) return 0;
  struct timeval now;
  gettimeofday(&now, NULL);
  return (NSTimeInterval)(now.tv_sec - boot.tv_sec);
}

+ (NSString*)wifiAddress {
  struct ifaddrs* list = NULL;
  NSString* result = @"";
  if (getifaddrs(&list) != 0) return result;
  for (struct ifaddrs* ifa = list; ifa; ifa = ifa->ifa_next) {
    if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET) continue;
    if (strcmp(ifa->ifa_name, "en0") != 0) continue;
    char buf[INET_ADDRSTRLEN];
    struct sockaddr_in* sin = (struct sockaddr_in*)ifa->ifa_addr;
    if (inet_ntop(AF_INET, &sin->sin_addr, buf, sizeof buf)) result = [NSString stringWithUTF8String:buf];
    break;
  }
  freeifaddrs(list);
  return result;
}

+ (NSString*)batteryString {
  UIDevice* dev = [UIDevice currentDevice];
  float level = dev.batteryLevel;
  if (level < 0) return @"n/a";
  NSString* st = @"";
  switch (dev.batteryState) {
    case UIDeviceBatteryStateCharging: st = @" charging"; break;
    case UIDeviceBatteryStateFull: st = @" full"; break;
    case UIDeviceBatteryStateUnplugged: st = @" on battery"; break;
    default: break;
  }
  return [NSString stringWithFormat:@"%d%%%@", (int)(level * 100.0f + 0.5f), st];
}

+ (NSString*)formatTime:(long long)ms {
  if (ms < 0) ms = 0;
  long long s = ms / 1000;
  return [NSString stringWithFormat:@"%lld:%02lld", s / 60, s % 60];
}

+ (NSString*)formatDuration:(NSTimeInterval)secs {
  long long s = (long long)secs;
  long long d = s / 86400;
  s %= 86400;
  if (d > 0) return [NSString stringWithFormat:@"%lldd %02lld:%02lld:%02lld", d, s / 3600, (s / 60) % 60, s % 60];
  return [NSString stringWithFormat:@"%02lld:%02lld:%02lld", s / 3600, (s / 60) % 60, s % 60];
}

@end
