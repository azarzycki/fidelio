#import "FidelioAppDelegate.h"

#include <spawn.h>
#include <sys/wait.h>

extern char** environ;

@implementation FidelioAppDelegate

- (void)applicationDidFinishLaunching:(UIApplication*)application {
  [application setStatusBarHidden:YES animated:NO];
  [application setIdleTimerDisabled:YES];  // a dock display must not auto-lock

  status = [[FidelioStatus alloc] init];
  [status refresh];

  nowPlaying = [[NowPlayingViewController alloc] initWithStatus:status];
  technical = [[TechnicalViewController alloc] initWithStatus:status];
  // The Settings tab lives inside a navigation controller so an enum option can push a picker.
  settingsVC = [[SettingsViewController alloc] init];
  settingsNav = [[UINavigationController alloc] initWithRootViewController:settingsVC];
  settingsNav.title = @"Settings";
  UIImage* settingsIcon = [[UIImage alloc] initWithContentsOfFile:[[NSBundle mainBundle] pathForResource:@"tab_settings" ofType:@"png"]];
  settingsNav.tabBarItem = [[[UITabBarItem alloc] initWithTitle:@"Settings" image:settingsIcon tag:2] autorelease];
  [settingsIcon release];

  tabs = [[UITabBarController alloc] init];
  tabs.viewControllers = [NSArray arrayWithObjects:nowPlaying, technical, settingsNav, nil];

  window = [[UIWindow alloc] initWithFrame:[[UIScreen mainScreen] bounds]];
  window.backgroundColor = [UIColor blackColor];
  // B1.18: iOS 4+ routes rotation through the window's root view controller (the tablet rotates);
  // iPhone OS 3.1 has no rootViewController, so the tab bar's view is added directly as before.
  if ([window respondsToSelector:@selector(setRootViewController:)]) [window performSelector:@selector(setRootViewController:) withObject:tabs];
  else [window addSubview:tabs.view];
  [window makeKeyAndVisible];

  [self startTimer];
}

- (void)startTimer {
  if (timer) return;
  // The timer retains its target, so delegate <-> timer is a retain cycle; accepted, since UIApplication
  // keeps its delegate for the life of the process and -stopTimer breaks the cycle anyway.
  timer = [[NSTimer scheduledTimerWithTimeInterval:1.0 target:self selector:@selector(tick:) userInfo:nil repeats:YES] retain];
}

- (void)stopTimer {
  [timer invalidate];
  [timer release];
  timer = nil;
}

- (void)tick:(NSTimer*)t {
  [status refresh];
  [self watchdog];
  if (tabs.selectedViewController == nowPlaying) [nowPlaying update];
  else if (tabs.selectedViewController == technical) [technical update];
  else if (tabs.selectedViewController == settingsNav) [settingsVC update];
}

- (void)watchdog {
  // The watchdog is about cspot only, so it watches status-spotify.plist whichever source the
  // display shows: by its own "updated" field when it parses, by the file's mtime otherwise. A
  // file that is simply absent is not a hang (cspot not started yet).
  NSTimeInterval age = [status spotifyStatusAge];
  if (age >= 0) everValid = YES;
  BOOL stale = everValid && age > FIDELIO_WATCHDOG_STALE_S;
  if (!stale) return;
  NSTimeInterval now = CFAbsoluteTimeGetCurrent();
  if (lastProd != 0 && now - lastProd < FIDELIO_WATCHDOG_COOLDOWN_S) return;
  lastProd = now;

  // The app and cspot both run as mobile, so a plain killall works; launchd restarts cspot.
  char* argv[] = { (char*)"killall", (char*)"-9", (char*)FIDELIO_WATCHDOG_TARGET, NULL };
  pid_t pid = 0;
  int rc = posix_spawn(&pid, "/usr/bin/killall", NULL, NULL, argv, environ);
  int wstatus = -1;
  if (rc == 0) waitpid(pid, &wstatus, 0);
  NSLog(@"watchdog: status %.0f s old, killall -9 %s -> spawn %d, exit %d", age, FIDELIO_WATCHDOG_TARGET, rc,
        rc == 0 && WIFEXITED(wstatus) ? WEXITSTATUS(wstatus) : -1);
  [nowPlaying showNotice:@"Player not responding - restarting" forSeconds:8];
}

// iPhone OS 3 keeps the frontmost app running while the screen is locked (sleep button) or a call /
// alert is on top; nothing is visible then, so do not poll the file and relayout once a second.
// B1.16.4: the app's own URL scheme (nowplaying:, CFBundleURLTypes in Info.plist). fidelio-audiod passes it
// to SBSOpenSensitiveURLAndUnlock so that unlocking a locked device lands directly in this app. The URL's
// content is ignored: being opened is all it is for. (iOS 3.0-4.1 call this method; 4.2+ call it too
// when the newer openURL:sourceApplication:annotation: is not implemented.)
- (BOOL)application:(UIApplication*)application handleOpenURL:(NSURL*)url {
  return YES;
}

- (void)applicationWillResignActive:(UIApplication*)application {
  [self stopTimer];
}

- (void)applicationDidBecomeActive:(UIApplication*)application {
  [self tick:nil];  // catch up at once instead of showing the frame from before the lock
  [self startTimer];
}

- (void)applicationWillTerminate:(UIApplication*)application {
  [self stopTimer];
}

- (void)dealloc {
  [self stopTimer];
  [tabs release];
  [nowPlaying release];
  [technical release];
  [settingsVC release];
  [settingsNav release];
  [status release];
  [window release];
  [super dealloc];
}

@end
