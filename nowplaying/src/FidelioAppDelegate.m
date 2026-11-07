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
  [self watchdog];
  if (tabs.selectedViewController == nowPlaying) [nowPlaying update];
  else [technical update];
}

- (void)watchdog {
  if (status.dict) everValid = YES;
  // Age of the status: by its own "updated" field when it parses, by the file's mtime when it
  // exists but does not parse. A file that is simply absent is not a hang (cspot not started yet).
  NSTimeInterval age = [status fileAge];
  if (age < 0 && !status.fileMissing) age = [status fileMtimeAge];
  BOOL stale = age > FIDELIO_WATCHDOG_STALE_S && (everValid || !status.fileMissing);
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
