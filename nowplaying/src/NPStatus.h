// NPStatus: reads /var/mobile/dockreceiver/active-source (written by dock-audiod: the source
// that owns the speaker right now, or "none"), then that source's status-<source>.plist (written
// atomically by cspot-ios3 / dock-raop) and its cover-<source>.jpg, and gathers the device-side
// numbers for the Technical tab. Without audiod (no active-source file) it shows the most recently
// updated source. B1.16.16 (1.4.3): with nothing active it keeps showing audiod's remote target
// (remote-target: a paused source of ours, until it disconnects, stops, or other audio plays), as paused.
// B1.17 (1.5.0): the directory is DOCKRECEIVER_DIR (common/DataDir.h, /var/mobile/dockreceiver), and when
// it is missing, or nothing in it is fresh, the app says the daemons are not running (NPDaemonsState).
#import <UIKit/UIKit.h>

#include "common/DataDir.h"
#include "NPSource.h"

#define NP_DIR @DOCKRECEIVER_DIR  // the one default (common/DataDir.h)
#define NP_APP_VERSION @"1.5.0 (dock-receiver)"
#define NP_STALE_S 15.0          // status file older than this: the player is not updating it
#define NP_ACTIVE_SOURCE_STALE_S 30.0  // active-source older than this (audiod rewrites it every 10 s): audiod is gone
// B1.18: the cover is decoded to the size it is shown at: 256 px on the phone (memory: a deliberate
// limit), 600 px on the tablet ([NPStatus coverPixels], from NPLayout's NPCoverPixels).
#define NP_COVER_MAX_PX 1600.0f  // decoding is w*h*4 bytes; 1600^2 = 10 MB is the most this device should spare

@interface NPStatus : NSObject {
  NSDictionary* dict;      // last parsed status.plist, or nil
  UIImage* cover;          // current cover scaled to a coverPixels square, or nil
  NSString* coverName;     // "cover.jpg" or ""
  NSDate* coverMtime;      // mtime of the file at the last load attempt, nil if it was absent
  unsigned long long coverSize;
  NSDate* readTime;        // when `dict` was read (for smooth position)
  BOOL fileMissing;        // last refresh: the shown status-<source>.plist does not exist
  BOOL fileUnreadable;     // last refresh: it exists but is not a plist dictionary (writer bug)
  NSString* activeSource;  // audiod's active-source: "spotify" / "airplay" / "none"; "" when the file is absent
  NSString* shownSource;   // whose status-<source>.plist `dict` comes from
  BOOL pausedTarget;       // B1.16.16: shownSource is audiod's remote target and nothing is active
  BOOL idle;               // B1.16.16: audiod runs and has neither an active source nor a remote target
  NPDaemons daemons;       // B1.17: running / not running / no data directory (NPSource.c)
}
@property(nonatomic, readonly) NSDictionary* dict;
@property(nonatomic, readonly) UIImage* cover;
@property(nonatomic, readonly) BOOL fileMissing;
@property(nonatomic, readonly) BOOL fileUnreadable;
@property(nonatomic, readonly) NSString* activeSource;
@property(nonatomic, readonly) NSString* shownSource;
- (BOOL)audiodIdle;      // audiod says nothing owns the output and no paused source is the remote's target: show idle
- (BOOL)pausedTarget;    // B1.16.16: the shown source is paused and still the remote's target: keep its track up
- (NPDaemons)daemons;    // B1.17: NP_DAEMONS_RUNNING, or why the screen says "Daemons not running"
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
+ (UIImage*)newCoverFromFile:(NSString*)path;  // +1; coverPixels square, nil if unreadable/too big. Main thread only.
// B1.18: one universal app. A screen whose shorter side exceeds 600 pt is a tablet (decided once, from
// [UIScreen mainScreen].bounds: no UI_USER_INTERFACE_IDIOM in the 3.1 SDK).
+ (BOOL)isTablet;
+ (CGFloat)coverPixels;                                 // 256 phone, 600 tablet
+ (BOOL)allowsOrientation:(UIInterfaceOrientation)o;    // tablet: all four; phone: portrait only (as before)
// The tablet shows a table in a centred column at most 640 pt wide (readable rows instead of 768 or
// 1024 pt ones): returns a container holding t (t keeps its own color), or t itself on the phone.
+ (UIView*)columnViewForTable:(UITableView*)t background:(UIColor*)bg;
+ (NSString*)formatTime:(long long)ms;        // mm:ss
+ (NSString*)formatDuration:(NSTimeInterval)s;  // 1d 02:03:04 / 02:03:04
@end
