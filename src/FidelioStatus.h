// FidelioStatus: reads /var/mobile/fidelio/status.plist (written atomically by cspot-ios3)
// and cover.jpg, and gathers the device-side numbers for the Technical tab.
#import <UIKit/UIKit.h>

#define FIDELIO_DIR @"/var/mobile/fidelio"
#define FIDELIO_APP_VERSION @"1.1 (source-aware)"

@interface FidelioStatus : NSObject {
  NSDictionary* dict;      // last parsed status.plist, or nil
  UIImage* cover;          // current cover, or nil
  NSString* coverName;     // "cover.jpg" or ""
  NSDate* coverMtime;      // mtime of the file at the last load attempt, nil if it was absent
  unsigned long long coverSize;
  NSDate* readTime;        // when `dict` was read (for smooth position)
  BOOL fileMissing;
}
@property(nonatomic, readonly) NSDictionary* dict;
@property(nonatomic, readonly) UIImage* cover;
@property(nonatomic, readonly) BOOL fileMissing;

- (void)refresh;  // called once a second

// Status file fields (never nil; "" / 0 when unknown)
- (NSString*)string:(NSString*)key;
- (long long)integer:(NSString*)key;
- (NSString*)state;                 // idle|connected|playing|paused|stopped|"" (no file)
- (long long)positionMsNow;         // position_ms advanced by the time since the file was read
- (long long)durationMs;
- (NSTimeInterval)fileAge;          // now - updated, seconds (-1 if unknown)

// Device-side figures
+ (unsigned long)ownRssKB;
+ (unsigned long)freeMemoryKB;
+ (NSTimeInterval)uptime;
+ (NSString*)wifiAddress;
+ (NSString*)batteryString;
+ (NSString*)formatTime:(long long)ms;        // mm:ss
+ (NSString*)formatDuration:(NSTimeInterval)s;  // 1d 02:03:04 / 02:03:04
@end
