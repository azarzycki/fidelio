#import "NowPlayingLockPluginController.h"

#import <objc/message.h>
#import <objc/runtime.h>
#include <stdlib.h>

#include "LPLayout.h"
#include "common/DataDir.h"

#define LP_DIR @DOCKRECEIVER_DIR  // the one default (common/DataDir.h)
#define COVER_PX 256.0f
#define STALE_ACTIVE_S 30.0   // active-source older than this (audiod rewrites every 10 s) = audiod gone

// Everything that touches files or UIKit runs behind this: a throw inside SpringBoard is a respring.
#define GUARD(...) @try { __VA_ARGS__ } @catch (NSException* e) { NSLog(@"NowPlaying lock plugin caught: %@", e); }

// B1.16.3: NSLog is invisible on the iPad (no syslog there), so trace the lifecycle to a file when
// /var/mobile/dockreceiver/lockplugin.debug exists — the only way to tell "SpringBoard never called us"
// from "called and drew nothing". Cheap (a stat per call) and off unless the debug file is present.
static void LPTrace(NSString* line) {
  @try {
    static NSString* dbg = @DOCKRECEIVER_DIR "/lockplugin.debug";
    if (![[NSFileManager defaultManager] fileExistsAtPath:dbg]) return;
    NSString* log = @DOCKRECEIVER_DIR "/lockplugin.log";
    NSString* entry = [NSString stringWithFormat:@"%.3f %@\n", CFAbsoluteTimeGetCurrent(), line];
    NSFileHandle* h = [NSFileHandle fileHandleForWritingAtPath:log];
    if (!h) { [entry writeToFile:log atomically:NO encoding:NSUTF8StringEncoding error:NULL]; return; }
    [h seekToEndOfFile]; [h writeData:[entry dataUsingEncoding:NSUTF8StringEncoding]]; [h closeFile];
  } @catch (NSException* e) {}
}

@implementation NowPlayingLockPluginController

// SpringBoard calls this CLASS method and stores the result in its _awayViewPluginControllers dict
// (and later sends it -disable / -purgeView). We return a RETAINED singleton: one controller for the
// life of SpringBoard, stable across enable/disable cycles, so the dict's retain/release can never
// leave us with a dangling instance, and we never create a second one. The small permanent retain is
// deliberate (a lock-screen plugin lives as long as SpringBoard does).
+ (id)rootViewController {
  static NowPlayingLockPluginController* shared = nil;
  @try {
    if (!shared) shared = [[self alloc] init];
    LPTrace([NSString stringWithFormat:@"+rootViewController -> %@", shared]);
  } @catch (NSException* e) { LPTrace(@"+rootViewController EXCEPTION"); }
  return shared;
}

static NSString* Trim(NSString* s) {
  return s ? [s stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]] : @"";
}
static NSDate* Mtime(NSString* path) {
  return [[[NSFileManager defaultManager] attributesOfItemAtPath:path error:NULL] objectForKey:NSFileModificationDate];
}
static NSString* Str(NSDictionary* d, NSString* k) {
  id v = [d objectForKey:k];
  return [v isKindOfClass:[NSString class]] ? v : @"";
}
static long long Int(NSDictionary* d, NSString* k) {
  id v = [d objectForKey:k];
  return [v respondsToSelector:@selector(longLongValue)] ? [v longLongValue] : 0;
}
static NSString* MMSS(long long ms) {
  if (ms < 0) ms = 0;
  long long s = ms / 1000;
  return [NSString stringWithFormat:@"%lld:%02lld", s / 60, s % 60];
}

- (id)init {
  if ((self = [super init])) {
    source = @"";
    coverName = @"";
  }
  return self;
}

// --- view construction (manual retain/release) --------------------------------------------------
static UILabel* MakeLabel(CGFloat size, BOOL bold, UIColor* color, UITextAlignment align) {
  UILabel* l = [[UILabel alloc] initWithFrame:CGRectZero];
  l.backgroundColor = [UIColor clearColor];
  l.textColor = color;
  l.font = bold ? [UIFont boldSystemFontOfSize:size] : [UIFont systemFontOfSize:size];
  l.textAlignment = align;
  return l;  // +1, caller owns
}

- (void)loadView {
  LPTrace(@"loadView");
  GUARD(
    CGRect scr = [UIScreen mainScreen].bounds;  // 320x480 on the phone, 768x1024 on the iPad
    content = [[UIView alloc] initWithFrame:scr];
    content.backgroundColor = [UIColor clearColor];
    content.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
    content.userInteractionEnabled = NO;  // never swallow the slide-to-unlock gesture

    coverView = [[UIImageView alloc] initWithFrame:CGRectZero];
    coverView.contentMode = UIViewContentModeScaleAspectFit;
    [content addSubview:coverView];

    sourceLabel = MakeLabel(15, NO, [UIColor colorWithWhite:0.7f alpha:1], UITextAlignmentCenter);
    titleLabel = MakeLabel(26, YES, [UIColor whiteColor], UITextAlignmentCenter);
    artistLabel = MakeLabel(20, NO, [UIColor colorWithWhite:0.88f alpha:1], UITextAlignmentCenter);
    albumLabel = MakeLabel(16, NO, [UIColor colorWithWhite:0.72f alpha:1], UITextAlignmentCenter);
    [content addSubview:sourceLabel];
    [content addSubview:titleLabel];
    [content addSubview:artistLabel];
    [content addSubview:albumLabel];

    track = [[UIView alloc] initWithFrame:CGRectZero];
    track.backgroundColor = [UIColor colorWithWhite:1 alpha:0.25f];
    trackFill = [[UIView alloc] initWithFrame:CGRectZero];
    trackFill.backgroundColor = [UIColor whiteColor];
    [track addSubview:trackFill];
    [content addSubview:track];

    elapsedLabel = MakeLabel(13, NO, [UIColor colorWithWhite:0.8f alpha:1], UITextAlignmentLeft);
    remainingLabel = MakeLabel(13, NO, [UIColor colorWithWhite:0.8f alpha:1], UITextAlignmentRight);
    [content addSubview:elapsedLabel];
    [content addSubview:remainingLabel];

    idleLabel = MakeLabel(18, NO, [UIColor colorWithWhite:0.6f alpha:1], UITextAlignmentCenter);
    idleLabel.text = @"NowPlaying";
    [content addSubview:idleLabel];

    [self setView:content];
    [self layout];
  )
}

- (id)view {
  if (!content) [self loadView];
  return content;
}

static CGRect CR(LPRect r) { return CGRectMake(r.x, r.y, r.w, r.h); }

// Lay the content out between the system clock/date (top) and slide-to-unlock (bottom): we are not
// fullscreen. B1.16.7: the frames come from LPLayout.c. The tablet is unchanged from B1.16.3; the
// 320x480 phone gets its own metrics (B1.16.3 put the text under the unlock bar there).
- (void)layout {
  GUARD(
    if (!content) return;
    CGRect b = content.bounds;
    CGRect s = [UIScreen mainScreen].bounds;
    LPLayout L;
    LPLayoutCompute(b.size.width, b.size.height, LPIsTablet(s.size.width, s.size.height), &L);
    coverView.frame = CR(L.cover);
    sourceLabel.frame = CR(L.source);
    titleLabel.frame = CR(L.title);
    artistLabel.frame = CR(L.artist);
    albumLabel.frame = CR(L.album);
    track.frame = CR(L.track);
    elapsedLabel.frame = CR(L.elapsed);
    remainingLabel.frame = CR(L.remaining);
    idleLabel.frame = CR(L.idle);
    sourceLabel.font = [UIFont systemFontOfSize:L.sourceFont];
    titleLabel.font = [UIFont boldSystemFontOfSize:L.titleFont];
    artistLabel.font = [UIFont systemFontOfSize:L.artistFont];
    albumLabel.font = [UIFont systemFontOfSize:L.albumFont];
    elapsedLabel.font = [UIFont systemFontOfSize:L.timeFont];
    remainingLabel.font = [UIFont systemFontOfSize:L.timeFont];
    idleLabel.font = [UIFont systemFontOfSize:L.idleFont];
    LPTrace([NSString stringWithFormat:@"layout: view %.0fx%.0f, screen %.0fx%.0f, %@: cover %.0f at y %.0f, text to y %.0f",
             b.size.width, b.size.height, s.size.width, s.size.height, L.tablet ? @"tablet" : @"phone", L.cover.w, L.cover.y,
             L.elapsed.y + L.elapsed.h]);
  )
}

- (void)setOrientation:(int)o {
  GUARD([super setOrientation:o]; [self layout];)
}

// --- visibility: refresh once a second ONLY while on screen ---------------------------------------
- (void)viewWillAppear:(BOOL)animated {
  LPTrace(@"viewWillAppear");
  GUARD([super viewWillAppear:animated]; [self layout];)
}
- (void)viewDidAppear:(BOOL)animated {
  LPTrace(@"viewDidAppear");
  GUARD(
    [super viewDidAppear:animated];
    visible = YES;
    [self refresh];
    [timer invalidate]; [timer release];
    timer = [[NSTimer scheduledTimerWithTimeInterval:1.0 target:self selector:@selector(refresh) userInfo:nil repeats:YES] retain];
  )
}
- (void)stopTimer {
  [timer invalidate]; [timer release]; timer = nil; visible = NO;
}
- (void)viewWillDisappear:(BOOL)animated { GUARD([super viewWillDisappear:animated]; [self stopTimer];) }
- (void)viewDidDisappear:(BOOL)animated  { LPTrace(@"viewDidDisappear"); GUARD([super viewDidDisappear:animated]; [self stopTimer];) }
- (void)disable { LPTrace(@"disable"); GUARD([self stopTimer]; [super disable];) }

- (void)loadPrefs {
  NSString* p = [LP_DIR stringByAppendingPathComponent:@"lockplugin.plist"];
  NSDate* m = Mtime(p);
  if (m && (!prefsMtime || ![m isEqualToDate:prefsMtime])) {
    NSDictionary* d = [NSDictionary dictionaryWithContentsOfFile:p];
    keepScreenOn = [[d objectForKey:@"KeepScreenOn"] boolValue];
    [prefsMtime release]; prefsMtime = [m retain];
  } else if (!m) {
    keepScreenOn = NO;  // absent file = defaults
    [prefsMtime release]; prefsMtime = nil;
  }
}

- (NSString*)pickSource {
  NSString* ap = [LP_DIR stringByAppendingPathComponent:@"active-source"];
  NSString* a = Trim([NSString stringWithContentsOfFile:ap encoding:NSUTF8StringEncoding error:NULL]);
  NSDate* m = Mtime(ap);
  if (!m || [[NSDate date] timeIntervalSinceDate:m] > STALE_ACTIVE_S) return @"";
  if ([a length] == 0 || [a isEqualToString:@"none"]) return @"";
  return a;
}

- (void)refresh {
  if (!visible) return;
  GUARD(
    [self loadPrefs];
    NSString* src = [self pickSource];
    if (![src isEqualToString:source]) { [source release]; source = [src copy]; [status release]; status = nil; }
    BOOL had = haveContent;
    haveContent = [source length] > 0;
    NSString* lit = [self holdScreenLit];
    LPTrace([NSString stringWithFormat:@"refresh: pickSource='%@' haveContent=%d keepLit=%@", src, (int)haveContent, lit]);
    // B1.16.3: the base class may cache displayability; keep it in sync when our content appears/goes.
    if (haveContent != had) { @try { [self setViewCanBeDisplayed:haveContent]; } @catch (NSException* e) {} }

    if (haveContent) {
      NSString* path = [LP_DIR stringByAppendingPathComponent:[NSString stringWithFormat:@"status-%@.plist", source]];
      NSDictionary* d = [NSDictionary dictionaryWithContentsOfFile:path];  // rename()d into place, never half-written
      if (d) { [status release]; status = [d retain]; [readTime release]; readTime = [[NSDate alloc] init]; }
    }
    [self render];
  )
}

- (void)render {
  BOOL show = haveContent && status != nil;
  idleLabel.hidden = show;
  coverView.hidden = !show;
  sourceLabel.hidden = !show; titleLabel.hidden = !show; artistLabel.hidden = !show; albumLabel.hidden = !show;
  track.hidden = !show; elapsedLabel.hidden = !show; remainingLabel.hidden = !show;
  if (!show) { [self updateCover:@""]; return; }

  NSString* pretty = [source isEqualToString:@"airplay"] ? @"AirPlay" : [source isEqualToString:@"spotify"] ? @"Spotify" : source;
  sourceLabel.text = pretty;
  titleLabel.text = Str(status, @"title");
  artistLabel.text = Str(status, @"artist");
  albumLabel.text = Str(status, @"album");

  long long dur = Int(status, @"duration_ms");
  long long pos = Int(status, @"position_ms");
  if ([Str(status, @"state") isEqualToString:@"playing"] && readTime)
    pos += (long long)([[NSDate date] timeIntervalSinceDate:readTime] * 1000.0);
  if (dur > 0 && pos > dur) pos = dur;
  elapsedLabel.text = MMSS(pos);
  remainingLabel.text = dur > 0 ? [@"-" stringByAppendingString:MMSS(dur - pos)] : @"";
  CGFloat frac = dur > 0 ? (CGFloat)pos / (CGFloat)dur : 0;
  CGRect tb = track.bounds;
  trackFill.frame = CGRectMake(0, 0, tb.size.width * frac, tb.size.height);

  [self updateCover:Str(status, @"cover")];
}

// Reload the cover only when (name, mtime, size) changes; cap at COVER_PX; a bad file -> no image,
// never the previous track's art.
- (void)updateCover:(NSString*)name {
  NSString* coverPath = [name length] ? [LP_DIR stringByAppendingPathComponent:name] : nil;
  NSDictionary* attrs = coverPath ? [[NSFileManager defaultManager] attributesOfItemAtPath:coverPath error:NULL] : nil;
  NSDate* mtime = [attrs objectForKey:NSFileModificationDate];
  unsigned long long size = [[attrs objectForKey:NSFileSize] unsignedLongLongValue];
  BOOL changed = ![name isEqualToString:coverName] ||
                 !(mtime == coverMtime || (mtime && coverMtime && [mtime isEqualToDate:coverMtime])) || size != coverSize;
  if (!changed) return;
  UIImage* img = nil;
  if (mtime && size > 0 && size < 8u * 1024 * 1024) {
    UIImage* raw = [UIImage imageWithContentsOfFile:coverPath];
    if (raw) {
      UIGraphicsBeginImageContext(CGSizeMake(COVER_PX, COVER_PX));
      [raw drawInRect:CGRectMake(0, 0, COVER_PX, COVER_PX)];
      img = [UIGraphicsGetImageFromCurrentImageContext() retain];  // +1
      UIGraphicsEndImageContext();
    }
  }
  coverView.image = img;  // UIImageView retains
  [img release];
  [coverName release]; coverName = [name copy];
  [coverMtime release]; coverMtime = [mtime retain];
  coverSize = size;
}

// --- base-class policy ---------------------------------------------------------------------------
- (BOOL)canScreenDim {
  // KeepScreenOn (lockplugin.plist) + something actually playing: keep the screen lit. Default: dim.
  // B1.16.6: the 5.1.1 SpringBoard ignores this vote for a plugin that is not full-screen (the lock
  // screen dimmed after a few seconds with it saying NO); -holdScreenLit does the work there.
  @try { return !(keepScreenOn && haveContent); } @catch (NSException* e) { return YES; }
}

// B1.16.6: KeepScreenOn for real. While KeepScreenOn is set and something plays, every refresh (1 Hz,
// and only while this plugin is on screen) restarts SpringBoard's lock-screen dim countdown. Nothing
// needs undoing. When the music stops, KeepScreenOn is turned off, or the plugin leaves the screen, the
// calls stop and the countdown runs out as usual. A lock screen that is already dimming or off
// (-isDimmed, e.g. after the sleep button) is left alone, so the sleep button still wins.
// B1.16.7: the 5.1.1 iPad has -restartDimTimer. The 3.1.3 SpringBoard has -restartDimTimer: (the no-arg
// form is unconfirmed there), so that is the fallback. It is used only when the runtime says its
// argument is a float or a double (method_copyArgumentType: never called with a guessed type), with
// 10 s, i.e. the screen dims at most ~10 s after the music stops. Resolved once; the trace logs what
// was found, including the type encoding. Returns what it did, for the debug trace: off,
// held / held:arg-float / held:arg-double, dimmed, unavailable.
enum { KL_UNRESOLVED, KL_NOARG, KL_ARG_FLOAT, KL_ARG_DOUBLE, KL_NONE };
static int keepLitPath = KL_UNRESOLVED;

static void ResolveKeepLit(id away) {
  Method m;
  if ([away respondsToSelector:@selector(restartDimTimer)]) {
    keepLitPath = KL_NOARG;
    LPTrace(@"keep-lit: using -[SBAwayController restartDimTimer]");
    return;
  }
  m = class_getInstanceMethod(object_getClass(away), @selector(restartDimTimer:));
  if (m) {
    char* arg = method_copyArgumentType(m, 2);  // 0 = self, 1 = _cmd, 2 = the argument
    const char* enc = method_getTypeEncoding(m);
    keepLitPath = (arg && arg[0] == 'f') ? KL_ARG_FLOAT : (arg && arg[0] == 'd') ? KL_ARG_DOUBLE : KL_NONE;
    LPTrace([NSString stringWithFormat:@"keep-lit: -[SBAwayController restartDimTimer:] has types '%s' (argument '%s'): %@",
             enc ? enc : "?", arg ? arg : "?", keepLitPath == KL_NONE ? @"not a float/double, NOT used" : @"used with 10 s"]);
    if (arg) free(arg);
    return;
  }
  keepLitPath = KL_NONE;
  LPTrace(@"keep-lit: SBAwayController has neither -restartDimTimer nor -restartDimTimer: (the lock screen dims as usual)");
}

- (NSString*)holdScreenLit {
  if (!(keepScreenOn && haveContent)) return @"off";
  Class cls = objc_getClass("SBAwayController");
  id<NowPlayingSBAwayController> away = nil;
  if (cls && [(id)cls respondsToSelector:@selector(sharedAwayController)]) away = [(id)cls sharedAwayController];
  if (!away) return @"unavailable";
  if (keepLitPath == KL_UNRESOLVED) ResolveKeepLit(away);
  if (keepLitPath == KL_NONE) return @"unavailable";
  if ([away respondsToSelector:@selector(isDimmed)] && [away isDimmed]) return @"dimmed";
  switch (keepLitPath) {
    case KL_NOARG: [away restartDimTimer]; return @"held";
    case KL_ARG_FLOAT: ((void (*)(id, SEL, float))objc_msgSend)(away, @selector(restartDimTimer:), 10.0f); return @"held:arg-float";
    case KL_ARG_DOUBLE: ((void (*)(id, SEL, double))objc_msgSend)(away, @selector(restartDimTimer:), 10.0); return @"held:arg-double";
  }
  return @"unavailable";
}
- (BOOL)showDateView { return YES; }                 // keep the system clock/date
- (BOOL)showAwayItems { return YES; }                // keep slide-to-unlock etc.
- (BOOL)viewWantsFullscreenLayout { return NO; }     // coexist with the date and slider, not cover them
- (BOOL)wantsAutomaticFullscreenTimer { return NO; }
- (BOOL)wantsSwipeGestureRecognizer { return NO; }   // we never take the unlock swipe
- (BOOL)canBeAlwaysFullscreen { return NO; }
- (BOOL)viewCanBeDisplayed { @try { return [self pickSource].length > 0; } @catch (NSException* e) { return NO; } }
- (int)pluginPriority { return 0; }                  // modest; tune on the device vs Apple's plugins
- (BOOL)retainsPriorityWhileInactive { return NO; }
- (BOOL)shouldShowLockStatusBarTime { return YES; }
- (BOOL)shouldDisableOnUnlock { return YES; }        // gone once unlocked
- (BOOL)shouldDisableOnRelock { return NO; }

- (void)purgeView {
  LPTrace(@"purgeView");
  GUARD(
    [self stopTimer];
    [coverView release]; coverView = nil;
    [sourceLabel release]; sourceLabel = nil;
    [titleLabel release]; titleLabel = nil;
    [artistLabel release]; artistLabel = nil;
    [albumLabel release]; albumLabel = nil;
    [trackFill release]; trackFill = nil;
    [track release]; track = nil;
    [elapsedLabel release]; elapsedLabel = nil;
    [remainingLabel release]; remainingLabel = nil;
    [idleLabel release]; idleLabel = nil;
    [content release]; content = nil;
    [self setView:nil];
  )
}

- (void)dealloc {
  [self stopTimer];
  [status release];
  [source release];
  [readTime release];
  [coverName release];
  [coverMtime release];
  [prefsMtime release];
  [self purgeView];
  [super dealloc];
}
@end
