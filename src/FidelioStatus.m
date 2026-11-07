#import "FidelioStatus.h"

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <mach/mach.h>
#include <mach/mach_host.h>
#include <net/if.h>
#include <netinet/in.h>
#include <sys/sysctl.h>
#include <sys/time.h>

static BOOL SameDate(NSDate* a, NSDate* b) {
  return a == b || (a && b && [a isEqualToDate:b]);
}

@implementation FidelioStatus

@synthesize dict, cover, fileMissing, fileUnreadable;

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
    fileUnreadable = NO;
  } else {
    // Keep the last good dict so the screen does not go blank. The writer renames the file into
    // place, so a half-written file is never seen: nil means absent (writer not running yet) or
    // not a plist dictionary at all (a writer bug worth showing rather than hiding as "no file").
    fileMissing = ![[NSFileManager defaultManager] fileExistsAtPath:path];
    fileUnreadable = !fileMissing;
  }

  // Cover: reload when the (name, mtime, size) of the file differs from the last load attempt.
  // HFS+ keeps mtime in whole seconds, so two covers written within the same second can only
  // be told apart by their size. A load that fails (missing, 0-byte or corrupt file) leaves
  // cover nil -- the placeholder, not the previous track's art -- and is not retried until the
  // file changes again.
  NSString* name = [self string:@"cover"];
  NSString* coverPath = nil;
  NSDate* mtime = nil;
  unsigned long long size = 0;
  if ([name length]) {
    coverPath = [FIDELIO_DIR stringByAppendingPathComponent:name];
    NSDictionary* attrs = [[NSFileManager defaultManager] attributesOfItemAtPath:coverPath error:NULL];
    mtime = [attrs objectForKey:NSFileModificationDate];
    size = [[attrs objectForKey:NSFileSize] unsignedLongLongValue];
  }
  BOOL changed = ![name isEqualToString:coverName] || !SameDate(mtime, coverMtime) || size != coverSize;
  if (changed) {
    [cover release];
    cover = mtime ? [FidelioStatus newCoverFromFile:coverPath] : nil;
    [coverName release];
    coverName = [name copy];
    [coverMtime release];
    coverMtime = [mtime retain];
    coverSize = size;
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

// UIImage keeps the file-backed CGImage, and the first draw decodes it to a w*h*4 bitmap that
// stays resident (a 640x640 Spotify cover is 1.6 MB, a 1400x1400 AirPlay/iTunes one 7.8 MB,
// 3000x3000 is 36 MB -- a jetsam kill on 116 MB). Decode once into a FIDELIO_COVER_PX square
// (aspect-fill, 256 KB), then drop the original. Dimensions come from the JPEG header, so
// oversized files are refused before anything is decoded. UIGraphics contexts are
// main-thread-only on iPhone OS 3; refresh runs from the main-thread timer.
+ (UIImage*)newCoverFromFile:(NSString*)path {
  UIImage* img = [[UIImage alloc] initWithContentsOfFile:path];
  if (!img) return nil;
  CGSize sz = img.size;
  if (sz.width < 1 || sz.height < 1 || sz.width > FIDELIO_COVER_MAX_PX || sz.height > FIDELIO_COVER_MAX_PX) {
    [img release];
    return nil;
  }
  CGFloat scale = FIDELIO_COVER_PX / (sz.width < sz.height ? sz.width : sz.height);
  CGFloat w = sz.width * scale, h = sz.height * scale;
  UIGraphicsBeginImageContext(CGSizeMake(FIDELIO_COVER_PX, FIDELIO_COVER_PX));
  [img drawInRect:CGRectMake((FIDELIO_COVER_PX - w) / 2, (FIDELIO_COVER_PX - h) / 2, w, h)];
  UIImage* small = [UIGraphicsGetImageFromCurrentImageContext() retain];
  UIGraphicsEndImageContext();
  [img release];
  return small;
}

- (NSTimeInterval)fileMtimeAge {
  NSString* path = [FIDELIO_DIR stringByAppendingPathComponent:@"status.plist"];
  NSDate* mtime = [[[NSFileManager defaultManager] attributesOfItemAtPath:path error:NULL] objectForKey:NSFileModificationDate];
  return mtime ? [[NSDate date] timeIntervalSinceDate:mtime] : -1;
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
