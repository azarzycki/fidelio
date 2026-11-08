#import <UIKit/UIKit.h>
#import "FidelioStatus.h"
#import "NowPlayingViewController.h"
#import "TechnicalViewController.h"

// Watchdog: if the status file stops being updated for this long, cspot is hung or dead and the
// app prods it with `killall -9 cspot-ios3` (launchd's KeepAlive relaunches it). Only stale files
// count, never a missing one at first launch, and at most once per cooldown. This catches hangs
// and crashes only: a player that keeps writing status (e.g. a wedged audio route) is not touched.
#define FIDELIO_WATCHDOG_STALE_S 30.0
#define FIDELIO_WATCHDOG_COOLDOWN_S 60.0
#define FIDELIO_WATCHDOG_TARGET "cspot-ios3"

@interface FidelioAppDelegate : NSObject <UIApplicationDelegate> {
  UIWindow* window;
  UITabBarController* tabs;
  FidelioStatus* status;
  NowPlayingViewController* nowPlaying;
  TechnicalViewController* technical;
  NSTimer* timer;
  BOOL everValid;            // status-spotify.plist has been seen since launch
  NSTimeInterval lastProd;   // CFAbsoluteTime of the last killall, 0 = never
}
- (void)watchdog;
@end
