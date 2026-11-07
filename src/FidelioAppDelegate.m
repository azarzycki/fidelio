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

  timer = [[NSTimer scheduledTimerWithTimeInterval:1.0 target:self selector:@selector(tick:) userInfo:nil repeats:YES] retain];
}

- (void)tick:(NSTimer*)t {
  [status refresh];
  if (tabs.selectedViewController == nowPlaying) [nowPlaying update];
  else [technical update];
}

- (void)applicationWillTerminate:(UIApplication*)application {
  [timer invalidate];
}

- (void)dealloc {
  [timer release];
  [tabs release];
  [nowPlaying release];
  [technical release];
  [status release];
  [window release];
  [super dealloc];
}

@end
