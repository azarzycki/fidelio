// FidelioStatus: reads /var/mobile/fidelio/active-source (written by fidelio-audiod: the source
// that owns the speaker right now, or "none"), then that source's status-<source>.plist (written
// atomically by cspot-ios3 / fidelio-raop) and its cover-<source>.jpg, and gathers the device-side
// numbers for the Technical tab. Without audiod (no active-source file) it shows the most recently
// updated source.
#import <UIKit/UIKit.h>

#define FIDELIO_DIR @"/var/mobile/fidelio"
#define FIDELIO_APP_VERSION @"1.2.1 (audiod)"
#define FIDELIO_STALE_S 15.0          // status file older than this: the player is not updating it
#define FIDELIO_ACTIVE_SOURCE_STALE_S 30.0  // active-source older than this (audiod rewrites it every 10 s): audiod is gone
#define FIDELIO_COVER_PX 256.0f       // the cover is shown at 256x256 (non-Retina), so that is all we keep
#define FIDELIO_COVER_MAX_PX 1600.0f  // decoding is w*h*4 bytes; 1600^2 = 10 MB is the most this device should spare

@interface FidelioStatus : NSObject {
  NSDictionary* dict;      // last parsed status.plist, or nil
  UIImage* cover;          // current cover scaled to FIDELIO_COVER_PX square, or nil
  NSString* coverName;     // "cover.jpg" or ""
  NSDate* coverMtime;      // mtime of the file at the last load attempt, nil if it was absent
  unsigned long long coverSize;
  NSDate* readTime;        // when `dict` was read (for smooth position)
  BOOL fileMissing;        // last refresh: the shown status-<source>.plist does not exist
  BOOL fileUnreadable;     // last refresh: it exists but is not a plist dictionary (writer bug)
  NSString* activeSource;  // audiod's active-source: "spotify" / "airplay" / "none"; "" when the file is absent
  NSString* shownSource;   // whose status-<source>.plist `dict` comes from
}
@property(nonatomic, readonly) NSDictionary* dict;
@property(nonatomic, readonly) UIImage* cover;
@property(nonatomic, readonly) BOOL fileMissing;
@property(nonatomic, readonly) BOOL fileUnreadable;
@property(nonatomic, readonly) NSString* activeSource;
@property(nonatomic, readonly) NSString* shownSource;
- (BOOL)audiodIdle;      // audiod says nothing owns the output: show idle whatever the files claim
- (NSTimeInterval)spotifyStatusAge;  // age of status-spotify.plist (updated field, else mtime); -1 if absent. Watchdog.

- (void)refresh;  // called once a second

// Status file fields (never nil; "" / 0 when unknown)
- (NSString*)string:(NSString*)key;
- (long long)integer:(NSString*)key;
- (NSString*)state;                 // idle|connected|playing|paused|stopped|"" (no file)
- (long long)positionMsNow;         // position_ms advanced by the time since the file was read
- (long long)durationMs;
- (NSTimeInterval)fileAge;          // now - updated, seconds (-1 if unknown)
- (NSTimeInterval)fileMtimeAge;     // now - mtime of the shown status plist, seconds (-1 if absent)

// Device-side figures
+ (unsigned long)ownRssKB;
+ (unsigned long)freeMemoryKB;
+ (NSTimeInterval)uptime;
+ (NSString*)wifiAddress;
+ (NSString*)batteryString;
+ (UIImage*)newCoverFromFile:(NSString*)path;  // +1; FIDELIO_COVER_PX square, nil if unreadable/too big. Main thread only.
+ (NSString*)formatTime:(long long)ms;        // mm:ss
+ (NSString*)formatDuration:(NSTimeInterval)s;  // 1d 02:03:04 / 02:03:04
@end
