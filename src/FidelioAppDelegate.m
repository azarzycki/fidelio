#import "FidelioAppDelegate.h"

@implementation FidelioAppDelegate

- (void)applicationDidFinishLaunching:(UIApplication*)application {
  [application setStatusBarHidden:YES animated:NO];
  [application setIdleTimerDisabled:YES];  // a dock display must not auto-lock

  status = [[FidelioStatus alloc] init];
  [status refresh];

  nowPlaying = [[NowPlayingViewController alloc] initWithStatus:status];
  technical = [[TechnicalViewController alloc] initWithStatus:status];

  tabs = [[UITabBarController alloc] init];
  tabs.viewControllers = [NSArray arrayWithObjects:nowPlaying, technical, nil];

  window = [[UIWindow alloc] initWithFrame:[[UIScreen mainScreen] bounds]];
  window.backgroundColor = [UIColor blackColor];
  [window addSubview:tabs.view];
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
  if (tabs.selectedViewController == nowPlaying) [nowPlaying update];
  else [technical update];
}

// iPhone OS 3 keeps the frontmost app running while the screen is locked (sleep button) or a call /
// alert is on top; nothing is visible then, so do not poll the file and relayout once a second.
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
  [status release];
  [window release];
  [super dealloc];
}

@end
