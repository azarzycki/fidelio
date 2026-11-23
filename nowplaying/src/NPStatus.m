#import "NPStatus.h"
#include "NPLayout.h"
#include "NPSource.h"

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <mach/mach.h>
#include <mach/mach_host.h>
#include <net/if.h>
#include <string.h>
#include <netinet/in.h>
#include <sys/sysctl.h>
#include <sys/time.h>

static BOOL SameDate(NSDate* a, NSDate* b) {
  return a == b || (a && b && [a isEqualToDate:b]);
}

@implementation NPStatus

@synthesize dict, cover, fileMissing, fileUnreadable, activeSource, shownSource;

static NSString* StatusPath(NSString* source) {
  return [NP_DIR stringByAppendingPathComponent:[NSString stringWithFormat:@"status-%@.plist", source]];
}

static NSDate* Mtime(NSString* path) {
  return [[[NSFileManager defaultManager] attributesOfItemAtPath:path error:NULL] objectForKey:NSFileModificationDate];
}

- (id)init {
  if ((self = [super init])) {
    coverName = @"";
    activeSource = @"";
    shownSource = @"spotify";
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
  [activeSource release];
  [shownSource release];
  [super dealloc];
}

- (BOOL)audiodIdle {
  return idle;
}

- (BOOL)pausedTarget {
  return pausedTarget;
}

- (NPDaemons)daemons {
  return daemons;
}

// One of audiod's one-line files, as a name; nil when it is absent or older than
// NP_ACTIVE_SOURCE_STALE_S (audiod rewrites both every 10 s: an older one is a leftover).
static NSString* FreshName(NSString* name) {
  NSString* path = [NP_DIR stringByAppendingPathComponent:name];
  NSDate* mtime = Mtime(path);
  if (!mtime || [[NSDate date] timeIntervalSinceDate:mtime] > NP_ACTIVE_SOURCE_STALE_S) return nil;
  NSString* text = [NSString stringWithContentsOfFile:path encoding:NSUTF8StringEncoding error:NULL];
  const char* c = text ? [text UTF8String] : NULL;
  if (!c) return nil;
  char buf[NP_SOURCE_NAME_MAX];
  NPSourceName(c, strlen(c), buf, sizeof buf);
  return [NSString stringWithUTF8String:buf];
}

// Which status file to show (NPSource.c decides): audiod's active source; else (B1.16.16) its remote
// target, a paused source of ours; else, when none is active (or audiod is not running), the source
// whose status file changed most recently, so the Technical tab still has data.
- (NSString*)pickSource {
  NSString* target = FreshName(@"remote-target");
  NPSourcePick pick;
  NPPickSource([activeSource UTF8String], target ? [target UTF8String] : NULL, &pick);
  pausedTarget = pick.paused ? YES : NO;
  idle = pick.idle ? YES : NO;
  if (pick.shown[0]) return [NSString stringWithUTF8String:pick.shown];
  NSDate* sp = Mtime(StatusPath(@"spotify"));
  NSDate* ap = Mtime(StatusPath(@"airplay"));
  if (ap && (!sp || [ap compare:sp] == NSOrderedDescending)) return @"airplay";
  return @"spotify";
}

- (void)refresh {
  // audiod rewrites the file every 10 s; one much older than that is a leftover of a dead audiod.
  NSString* act = FreshName(@"active-source");
  BOOL activeFresh = act != nil;
  if (!act) act = @"";
  [activeSource release];
  activeSource = [act copy];
  NSString* src = [self pickSource];
  if (![src isEqualToString:shownSource]) {
    [shownSource release];
    shownSource = [src copy];
    [dict release];
    dict = nil;  // never show the other source's track
  }
  NSString* path = StatusPath(shownSource);
  // B1.17: an empty or missing directory (nothing installed, not migrated, or the daemons stopped) is
  // shown as such, not as an empty screen.
  int dirExists = NPIsDirectory(DOCKRECEIVER_DIR);
  NSDate* statusMtime = Mtime(path);
  daemons = NPDaemonsState(dirExists, activeFresh ? 1 : 0,
                           statusMtime ? [[NSDate date] timeIntervalSinceDate:statusMtime] : -1.0, NP_ACTIVE_SOURCE_STALE_S);
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
    coverPath = [NP_DIR stringByAppendingPathComponent:name];
    NSDictionary* attrs = [[NSFileManager defaultManager] attributesOfItemAtPath:coverPath error:NULL];
    mtime = [attrs objectForKey:NSFileModificationDate];
    size = [[attrs objectForKey:NSFileSize] unsignedLongLongValue];
  }
  BOOL changed = ![name isEqualToString:coverName] || !SameDate(mtime, coverMtime) || size != coverSize;
  if (changed) {
    [cover release];
    cover = mtime ? [NPStatus newCoverFromFile:coverPath] : nil;
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
// 3000x3000 is 36 MB -- a jetsam kill on 116 MB). Decode once into a coverPixels square
// (aspect-fill, 256 KB), then drop the original. Dimensions come from the JPEG header, so
// oversized files are refused before anything is decoded. UIGraphics contexts are
// main-thread-only on iPhone OS 3; refresh runs from the main-thread timer.
+ (BOOL)isTablet {
  static int tablet = -1;
  if (tablet < 0) {
    CGSize s = [UIScreen mainScreen].bounds.size;
    tablet = NPIsTablet(s.width, s.height) ? 1 : 0;
  }
  return tablet == 1;
}

+ (CGFloat)coverPixels { return NPCoverPixels([self isTablet] ? 1 : 0); }

+ (BOOL)allowsOrientation:(UIInterfaceOrientation)o {
  return [self isTablet] || o == UIInterfaceOrientationPortrait;
}

+ (UIView*)columnViewForTable:(UITableView*)t background:(UIColor*)bg {
  if (![self isTablet]) return t;
  CGRect f = t.frame;
  UIView* box = [[[UIView alloc] initWithFrame:f] autorelease];
  box.backgroundColor = bg;
  box.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
  CGFloat w = f.size.width > 640 ? 640 : f.size.width;
  t.frame = CGRectMake((f.size.width - w) / 2, 0, w, f.size.height);
  t.autoresizingMask = UIViewAutoresizingFlexibleLeftMargin | UIViewAutoresizingFlexibleRightMargin | UIViewAutoresizingFlexibleHeight;
  // iOS 3.2+ tables draw a backgroundView over backgroundColor; drop it so the column and the margins
  // match (respondsToSelector: the 3.1 SDK and the phone's OS have no such property).
  if ([t respondsToSelector:@selector(setBackgroundView:)]) [t performSelector:@selector(setBackgroundView:) withObject:nil];
  t.backgroundColor = bg;
  [box addSubview:t];
  return box;
}

+ (UIImage*)newCoverFromFile:(NSString*)path {
  UIImage* img = [[UIImage alloc] initWithContentsOfFile:path];
  if (!img) return nil;
  CGSize sz = img.size;
  if (sz.width < 1 || sz.height < 1 || sz.width > NP_COVER_MAX_PX || sz.height > NP_COVER_MAX_PX) {
    [img release];
    return nil;
  }
  CGFloat px = [self coverPixels];
  CGFloat scale = px / (sz.width < sz.height ? sz.width : sz.height);
  CGFloat w = sz.width * scale, h = sz.height * scale;
  UIGraphicsBeginImageContext(CGSizeMake(px, px));
  [img drawInRect:CGRectMake((px - w) / 2, (px - h) / 2, w, h)];
  UIImage* small = [UIGraphicsGetImageFromCurrentImageContext() retain];
  UIGraphicsEndImageContext();
  [img release];
  return small;
}

- (NSTimeInterval)fileMtimeAge {
  NSDate* mtime = Mtime(StatusPath(shownSource));
  return mtime ? [[NSDate date] timeIntervalSinceDate:mtime] : -1;
}

- (NSTimeInterval)spotifyStatusAge {
  NSString* path = StatusPath(@"spotify");
  NSDictionary* d = [NSDictionary dictionaryWithContentsOfFile:path];
  id u = [d objectForKey:@"updated"];
  long long updated = [u respondsToSelector:@selector(longLongValue)] ? [u longLongValue] : 0;
  if (updated) return [[NSDate date] timeIntervalSince1970] - (NSTimeInterval)updated;
  NSDate* mtime = Mtime(path);
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
  int pct = (int)(level * 100.0f + 0.5f);
  NSString* st = @"";
  switch (dev.batteryState) {
    // At 100% a plugged-in device stops drawing charge, so iOS's own status bar reads "Not Charging"
    // even though the state is still Charging. Report that as "charged", not a misleading "charging"
    // (the iPad on a weak USB dock sat at "100% charging" forever otherwise).
    case UIDeviceBatteryStateCharging: st = pct >= 100 ? @" charged" : @" charging"; break;
    case UIDeviceBatteryStateFull: st = @" charged"; break;
    case UIDeviceBatteryStateUnplugged: st = @" on battery"; break;
    default: break;
  }
  return [NSString stringWithFormat:@"%d%%%@", pct, st];
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
