#import "NPEventProbe.h"
#import "NPStatus.h"  // NP_DIR
#import "NPRemoteControl.h"

static NSString* ProbeFlagPath(void) { return [NP_DIR stringByAppendingPathComponent:@"remote-probe.on"]; }
static NSString* ProbeLogPath(void)  { return [NP_DIR stringByAppendingPathComponent:@"remote-events.log"]; }

BOOL NPEventProbeEnabled(void) { return [[NSFileManager defaultManager] fileExistsAtPath:ProbeFlagPath()]; }
void NPEventProbeSetEnabled(BOOL on) {
  if (on) [[NSData data] writeToFile:ProbeFlagPath() atomically:YES];
  else [[NSFileManager defaultManager] removeItemAtPath:ProbeFlagPath() error:NULL];
}

static void appendLine(NSString* s) {
  @try {
    NSFileHandle* h = [NSFileHandle fileHandleForWritingAtPath:ProbeLogPath()];
    if (!h) { [s writeToFile:ProbeLogPath() atomically:NO encoding:NSUTF8StringEncoding error:NULL]; return; }
    [h seekToEndOfFile];
    [h writeData:[s dataUsingEncoding:NSUTF8StringEncoding]];
    [h closeFile];
  } @catch (NSException* e) {}
}

@implementation NPApplication

// B1.16.9: remote-control events (the iPad's dock keys, iOS 4+) are handled here before UIKit dispatches
// them, so they reach audiod whichever tab or responder is in front. They are also caught at the end of
// the responder chain below. NPRemoteControlHandle handles each event once and ignores everything else.
- (void)sendEvent:(UIEvent*)event {
  NPRemoteControlHandle(event, @"sendEvent");
  [super sendEvent:event];
  @try {
    static CFAbsoluteTime lastCheck = 0; static BOOL on = NO;
    CFAbsoluteTime now = CFAbsoluteTimeGetCurrent();
    if (now - lastCheck > 1.0) { on = NPEventProbeEnabled(); lastCheck = now; }
    if (!on) return;
    NSString* kind = @"event";
    int type = -1;
    @try { type = (int)event.type; } @catch (NSException* e) {}
    if (type == UIEventTypeTouches) {
      // Log only a tap's start/end, not every move, so the log is not flooded.
      UITouch* t = [[event allTouches] anyObject];
      if (t.phase != UITouchPhaseBegan && t.phase != UITouchPhaseEnded) return;
      kind = t.phase == UITouchPhaseBegan ? @"touch-down" : @"touch-up";
    } else {
      kind = [NSString stringWithFormat:@"NON-TOUCH event type=%d subtype=%d", type, (int)[event respondsToSelector:@selector(subtype)] ? (int)[(id)event subtype] : -1];
    }
    appendLine([NSString stringWithFormat:@"%.3f %@\n", now, kind]);
  } @catch (NSException* e) {}
}

// B1.16.9: the end of the responder chain (UIApplication follows the window).
- (void)remoteControlReceivedWithEvent:(UIEvent*)event {
  NPRemoteControlHandle(event, @"responder chain");
}

@end
