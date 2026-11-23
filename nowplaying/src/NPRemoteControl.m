#import "NPRemoteControl.h"
#import "NPStatus.h"  // NP_DIR, NP_APP_VERSION

#import <AVFoundation/AVFoundation.h>
#import <AudioToolbox/AudioToolbox.h>

#include <dlfcn.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>

// UIKit's remote-control constants (iOS 4.0). The iPhone OS 3.1.2 SDK predates them.
enum { NPEventTypeRemoteControl = 2 };
enum {
  NPRemotePlay = 100, NPRemotePause = 101, NPRemoteStop = 102, NPRemoteToggle = 103, NPRemoteNext = 104, NPRemotePrevious = 105,
  NPRemoteBeginSeekBack = 106, NPRemoteEndSeekBack = 107, NPRemoteBeginSeekFwd = 108, NPRemoteEndSeekFwd = 109,
};
// audiod/proto.h AUDIOD_CTRL_*
enum { CTRL_PLAY = 1, CTRL_PAUSE = 2, CTRL_TOGGLE = 3, CTRL_NEXT = 4, CTRL_PREV = 5 };

static BOOL active = NO;               // remote-control events are on (iOS 4+)
static AVAudioPlayer* silence = nil;   // stage 3 only

static void RCLog(NSString* line) {
  @try {
    NSString* path = [NP_DIR stringByAppendingPathComponent:@"remote-control.log"];
    NSString* entry = [NSString stringWithFormat:@"%.3f %@\n", CFAbsoluteTimeGetCurrent(), line];
    NSFileHandle* h = [NSFileHandle fileHandleForWritingAtPath:path];
    if (!h) { [entry writeToFile:path atomically:NO encoding:NSUTF8StringEncoding error:NULL]; return; }
    [h seekToEndOfFile];
    [h writeData:[entry dataUsingEncoding:NSUTF8StringEncoding]];
    [h closeFile];
  } @catch (NSException* e) {}
}

@implementation NPWindow
- (BOOL)canBecomeFirstResponder { return active; }
@end

static int ReadStage(void) {
  NSString* s = [NSString stringWithContentsOfFile:[NP_DIR stringByAppendingPathComponent:@"remote-control.stage"]
                                          encoding:NSUTF8StringEncoding error:NULL];
  int n = s ? [s intValue] : 1;
  return n < 1 ? 1 : n > 3 ? 3 : n;
}

// A 1 s, 8 kHz, 16-bit mono WAV of zeros, built in memory (no file in the bundle).
static NSData* SilentWav(void) {
  const uint32_t rate = 8000, bytes = rate * 2;
  NSMutableData* d = [NSMutableData dataWithLength:44 + bytes];
  uint8_t* p = (uint8_t*)[d mutableBytes];
  uint32_t u32; uint16_t u16;
  memcpy(p, "RIFF", 4); u32 = 36 + bytes; memcpy(p + 4, &u32, 4); memcpy(p + 8, "WAVEfmt ", 8);
  u32 = 16; memcpy(p + 16, &u32, 4); u16 = 1; memcpy(p + 20, &u16, 2); u16 = 1; memcpy(p + 22, &u16, 2);
  memcpy(p + 24, &rate, 4); u32 = rate * 2; memcpy(p + 28, &u32, 4); u16 = 2; memcpy(p + 32, &u16, 2); u16 = 16; memcpy(p + 34, &u16, 2);
  memcpy(p + 36, "data", 4); memcpy(p + 40, &bytes, 4);
  return d;
}

static void StartMixableSilence(void) {
  OSStatus init = AudioSessionInitialize(NULL, NULL, NULL, NULL);  // 'init' (already initialized) is fine
  UInt32 cat = kAudioSessionCategory_MediaPlayback, mix = 1;
  OSStatus c = AudioSessionSetProperty(kAudioSessionProperty_AudioCategory, sizeof cat, &cat);
  OSStatus m = AudioSessionSetProperty(kAudioSessionProperty_OverrideCategoryMixWithOthers, sizeof mix, &mix);
  OSStatus a = AudioSessionSetActive(true);
  NSError* err = nil;
  silence = [[AVAudioPlayer alloc] initWithData:SilentWav() error:&err];
  silence.numberOfLoops = -1;
  BOOL played = [silence play];
  RCLog([NSString stringWithFormat:@"stage 3: AudioSessionInitialize %d, category MediaPlayback %d, MixWithOthers=1 %d, active %d; silent AVAudioPlayer %@ play -> %d",
                                   (int)init, (int)c, (int)m, (int)a, silence ? @"created" : [err description], (int)played]);
}

void NPRemoteControlSetup(UIApplication* app) {
  @try {
    int stage = ReadStage();
    if (![app respondsToSelector:@selector(beginReceivingRemoteControlEvents)]) return;  // iPhone OS 3.1: nothing to do
    [app performSelector:@selector(beginReceivingRemoteControlEvents)];
    active = YES;
    RCLog([NSString stringWithFormat:@"NowPlaying %@ launched: remote-control stage %d; beginReceivingRemoteControlEvents called", NP_APP_VERSION, stage]);
    if (stage >= 2) {
      void* mr = dlopen("/System/Library/PrivateFrameworks/MediaRemote.framework/MediaRemote", RTLD_LAZY);
      void (*setCan)(Boolean) = mr ? (void (*)(Boolean))dlsym(mr, "MRMediaRemoteSetCanBeNowPlayingApplication") : NULL;
      if (setCan) setCan(true);
      RCLog([NSString stringWithFormat:@"stage 2: MRMediaRemoteSetCanBeNowPlayingApplication %@", setCan ? @"called with true" : @"NOT found"]);
    }
    if (stage >= 3) StartMixableSilence();
  } @catch (NSException* e) { RCLog([NSString stringWithFormat:@"setup EXCEPTION %@", e]); }
}

void NPRemoteControlClaimFirstResponder(UIView* v) {
  if (!active || !v) return;
  @try {
    UIWindow* w = [v isKindOfClass:[UIWindow class]] ? (UIWindow*)v : v.window;
    if ([w isKindOfClass:[NPWindow class]] && ![w isFirstResponder]) [w becomeFirstResponder];
  } @catch (NSException* e) {}
}

static NSString* SubtypeName(int s) {
  switch (s) {
    case NPRemotePlay: return @"play";
    case NPRemotePause: return @"pause";
    case NPRemoteStop: return @"stop";
    case NPRemoteToggle: return @"toggle";
    case NPRemoteNext: return @"next";
    case NPRemotePrevious: return @"previous";
    case NPRemoteBeginSeekBack: return @"begin-seek-back";
    case NPRemoteEndSeekBack: return @"end-seek-back";
    case NPRemoteBeginSeekFwd: return @"begin-seek-forward";
    case NPRemoteEndSeekFwd: return @"end-seek-forward";
  }
  return @"?";
}

static uint8_t VerbFor(int s) {
  switch (s) {
    case NPRemotePlay: return CTRL_PLAY;
    case NPRemotePause: case NPRemoteStop: return CTRL_PAUSE;
    case NPRemoteToggle: return CTRL_TOGGLE;
    case NPRemoteNext: return CTRL_NEXT;
    case NPRemotePrevious: return CTRL_PREV;
  }
  return 0;  // seeking: logged, not forwarded
}

BOOL NPRemoteControlHandle(UIEvent* event, NSString* via) {
  @try {
    if (!active || !event || (int)event.type != NPEventTypeRemoteControl) return NO;
    // UIKit can hand the same event to sendEvent: and to the responder chain: handle it once.
    static NSTimeInterval lastTs = -1; static int lastSub = -1;
    int sub = (int)event.subtype;
    NSTimeInterval ts = event.timestamp;
    if (ts == lastTs && sub == lastSub) return YES;
    lastTs = ts; lastSub = sub;
    uint8_t verb = VerbFor(sub);
    if (verb) NPSendControl(verb);
    RCLog([NSString stringWithFormat:@"remote via %@: subtype %d (%@) -> %@", via, sub, SubtypeName(sub),
                                     verb ? [NSString stringWithFormat:@"audiod verb %d", (int)verb] : @"not forwarded"]);
    return YES;
  } @catch (NSException* e) {}
  return NO;
}

// One transport verb to audiod over its Unix socket: HELLO as the non-producing "control" source, then
// CONTROL. Short timeouts, so the UI never blocks for long. Mirrors common/AudiodControl. (Moved here from
// NowPlayingViewController in B1.16.9, unchanged, so the dock remote and the buttons share it.)
void NPSendControl(uint8_t verb) {
  NSString* sp = [NP_DIR stringByAppendingPathComponent:@"audiod.sock"];
  int s = socket(AF_UNIX, SOCK_STREAM, 0);
  if (s < 0) return;
  struct timeval to = { 0, 300 * 1000 };
  setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &to, sizeof to);
  setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &to, sizeof to);
  struct sockaddr_un a; memset(&a, 0, sizeof a); a.sun_family = AF_UNIX;
  strncpy(a.sun_path, [sp fileSystemRepresentation], sizeof a.sun_path - 1);
  if (connect(s, (struct sockaddr*)&a, sizeof a) != 0) { close(s); return; }
  // HELLO: header {type=1,len=40} + AudiodHello{ver=2,prio=0,ch=2,bits=16,rate=44100,name="control"}
  unsigned char hello[48]; memset(hello, 0, sizeof hello);
  unsigned int t = 1, len = 40; memcpy(hello, &t, 4); memcpy(hello + 4, &len, 4);
  hello[8] = 2; hello[9] = 0; hello[10] = 2; hello[11] = 16;
  unsigned int rate = 44100; memcpy(hello + 12, &rate, 4);
  strncpy((char*)hello + 16, "control", 31);
  unsigned char ctl[9]; unsigned int ct = 10, cl = 1; memcpy(ctl, &ct, 4); memcpy(ctl + 4, &cl, 4); ctl[8] = verb;
  if (write(s, hello, 48) == 48) write(s, ctl, 9);
  char buf[8]; recv(s, buf, sizeof buf, 0);  // let audiod read before close (returns on timeout/EOF)
  close(s);
  NSLog(@"sent transport control verb %d to audiod", (int)verb);
}
