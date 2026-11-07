#import <UIKit/UIKit.h>
#import "FidelioStatus.h"
#import "NowPlayingViewController.h"
#import "TechnicalViewController.h"

@interface FidelioAppDelegate : NSObject <UIApplicationDelegate> {
  UIWindow* window;
  UITabBarController* tabs;
  FidelioStatus* status;
  NowPlayingViewController* nowPlaying;
  TechnicalViewController* technical;
  NSTimer* timer;
}
@end
