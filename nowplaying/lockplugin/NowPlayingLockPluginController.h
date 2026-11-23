// NowPlaying lock-screen plugin for iOS 5.1.1 (iPad1,1). Principal class of NowPlaying.lockbundle, loaded
// by SpringBoard into /System/Library/SpringBoardPlugins/. It subclasses SpringBoard's private
// SBAwayViewPluginController, whose @implementation lives inside SpringBoard: the interface below is
// declared from the live-device dump (B1.11) and the class symbol is left undefined at link time
// (-undefined dynamic_lookup), resolved when SpringBoard loads the bundle.
//
// SAFETY: this code runs INSIDE SpringBoard, so a crash is a respring. Every entry point is wrapped
// in @try/@catch, missing/half-written status files are tolerated, no file work happens unless the
// view is on screen, and everything is released in purgeView/dealloc. Compiled against the iPhone OS
// 3.1.2 SDK (nothing iOS-5-only at compile time); UIKit classes used (UIView/UIImageView/UILabel/
// UIProgressView/UIImage) all exist in 3.1 and run on 5.1.
#import <UIKit/UIKit.h>

// --- SpringBoard's base class, declared from the 5.1.1 runtime dump (only what we use/override).
@interface SBAwayViewPluginController : NSObject
- (id)init;
- (id)view;
- (void)setView:(id)view;
- (void)loadView;
- (void)viewWillAppear:(BOOL)animated;
- (void)viewDidAppear:(BOOL)animated;
- (void)viewWillDisappear:(BOOL)animated;
- (void)viewDidDisappear:(BOOL)animated;
- (int)orientation;
- (void)setOrientation:(int)orientation;
- (void)disable;
- (void)purgeView;
- (BOOL)canScreenDim;
- (BOOL)showDateView;
- (BOOL)showAwayItems;
- (BOOL)viewCanBeDisplayed;
- (void)setViewCanBeDisplayed:(BOOL)can;  // B1.16.3: the base class caches visibility; set it when our content changes
- (BOOL)viewWantsFullscreenLayout;
- (BOOL)wantsAutomaticFullscreenTimer;
- (BOOL)wantsSwipeGestureRecognizer;
- (BOOL)canBeAlwaysFullscreen;
- (int)pluginPriority;
- (BOOL)retainsPriorityWhileInactive;
- (BOOL)shouldShowLockStatusBarTime;
- (BOOL)shouldDisableOnUnlock;
- (BOOL)shouldDisableOnRelock;
@end

// B1.16.6: SpringBoard's lock-screen controller, only what KeepScreenOn uses. These selectors are in the
// 5.1.1 runtime survey of SBAwayController. A protocol, not a class: the class is looked up by name at
// run time (objc_getClass) and each selector is checked with respondsToSelector:, so nothing is linked
// (the build's undefined-symbol check stays at the two SBAwayViewPluginController symbols) and a
// missing method is a no-op.
@protocol NowPlayingSBAwayController <NSObject>
+ (id)sharedAwayController;
- (void)restartDimTimer;  // restart the lock screen's dim countdown (the no-argument form)
- (BOOL)isDimmed;         // the lock screen is dimming or off
@end

@interface NowPlayingLockPluginController : SBAwayViewPluginController {
  UIView* content;          // our root view (also returned by -view)
  UIImageView* coverView;
  UILabel* sourceLabel;
  UILabel* titleLabel;
  UILabel* artistLabel;
  UILabel* albumLabel;
  UIView* track;            // progress background
  UIView* trackFill;        // progress fill
  UILabel* elapsedLabel;
  UILabel* remainingLabel;
  UILabel* idleLabel;
  NSTimer* timer;           // 1 Hz, only while visible
  BOOL visible;

  NSDictionary* status;     // last good status-<source>.plist
  NSString* source;         // "spotify" / "airplay" / "" (none)
  NSDate* readTime;         // when `status` was read (smooth position)
  NSString* coverName;      // filename we last loaded
  NSDate* coverMtime;
  unsigned long long coverSize;

  BOOL keepScreenOn;        // lockplugin.plist KeepScreenOn
  NSDate* prefsMtime;       // to reload the prefs file when it changes
  BOOL haveContent;         // a live source with a status file right now
}
// B1.16.3: SpringBoard gets the controller from the CLASS method +rootViewController (it does NOT
// alloc/init the principal class); without it the whole bundle is dropped before anything is created.
+ (id)rootViewController;
@end
