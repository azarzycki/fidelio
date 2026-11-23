#import <UIKit/UIKit.h>
#import "NPStatus.h"
#import "NowPlayingViewController.h"
#import "TechnicalViewController.h"
#import "SettingsViewController.h"

// Watchdog: if the status file stops being updated for this long, cspot is hung or dead and the
// app prods it with `killall -9 cspot-ios3` (launchd's KeepAlive relaunches it). Only stale files
// count, never a missing one at first launch, and at most once per cooldown. This catches hangs
// and crashes only: a player that keeps writing status (e.g. a wedged audio route) is not touched.
#define NP_WATCHDOG_STALE_S 30.0
#define NP_WATCHDOG_COOLDOWN_S 60.0
#define NP_WATCHDOG_TARGET "cspot-ios3"

@interface NPAppDelegate : NSObject <UIApplicationDelegate> {
  UIWindow* window;
  UITabBarController* tabs;
  NPStatus* status;
  NowPlayingViewController* nowPlaying;
  TechnicalViewController* technical;
  SettingsViewController* settingsVC;
  UINavigationController* settingsNav;
  NSTimer* timer;
  BOOL everValid;            // status-spotify.plist has been seen since launch
  NSTimeInterval lastProd;   // CFAbsoluteTime of the last killall, 0 = never
}
- (void)watchdog;
@end
