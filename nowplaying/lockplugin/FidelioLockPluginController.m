#import "FidelioLockPluginController.h"

#define FIDELIO_DIR @"/var/mobile/fidelio"
#define COVER_PX 256.0f
#define STALE_ACTIVE_S 30.0   // active-source older than this (audiod rewrites every 10 s) = audiod gone

// Everything that touches files or UIKit runs behind this: a throw inside SpringBoard is a respring.
#define GUARD(...) @try { __VA_ARGS__ } @catch (NSException* e) { NSLog(@"Fidelio lock plugin caught: %@", e); }

// B1.16.3: NSLog is invisible on the iPad (no syslog there), so trace the lifecycle to a file when
// /var/mobile/fidelio/lockplugin.debug exists — the only way to tell "SpringBoard never called us"
// from "called and drew nothing". Cheap (a stat per call) and off unless the debug file is present.
static void LPTrace(NSString* line) {
  @try {
    static NSString* dbg = @"/var/mobile/fidelio/lockplugin.debug";
    if (![[NSFileManager defaultManager] fileExistsAtPath:dbg]) return;
    NSString* log = @"/var/mobile/fidelio/lockplugin.log";
    NSString* entry = [NSString stringWithFormat:@"%.3f %@\n", CFAbsoluteTimeGetCurrent(), line];
    NSFileHandle* h = [NSFileHandle fileHandleForWritingAtPath:log];
    if (!h) { [entry writeToFile:log atomically:NO encoding:NSUTF8StringEncoding error:NULL]; return; }
    [h seekToEndOfFile]; [h writeData:[entry dataUsingEncoding:NSUTF8StringEncoding]]; [h closeFile];
  } @catch (NSException* e) {}
}

@implementation FidelioLockPluginController

// SpringBoard calls this CLASS method and stores the result in its _awayViewPluginControllers dict
// (and later sends it -disable / -purgeView). We return a RETAINED singleton: one controller for the
// life of SpringBoard, stable across enable/disable cycles, so the dict's retain/release can never
// leave us with a dangling instance, and we never create a second one. The small permanent retain is
// deliberate (a lock-screen plugin lives as long as SpringBoard does).
+ (id)rootViewController {
  static FidelioLockPluginController* shared = nil;
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
    idleLabel.text = @"Fidelio";
    [content addSubview:idleLabel];

    [self setView:content];
    [self layout];
  )
}

- (id)view {
  if (!content) [self loadView];
  return content;
}

// Lay the content out centred in the lower third of the lock screen, so the system clock/date stay
// at the top and slide-to-unlock stays at the bottom (we are not fullscreen).
- (void)layout {
  GUARD(
    if (!content) return;
    CGRect b = content.bounds;
    CGFloat W = b.size.width, H = b.size.height;
    CGFloat cover = W < H ? 220 : 180;          // smaller in landscape (less height)
    CGFloat cx = W / 2;
    CGFloat top = H * 0.42f;                     // below the clock
    coverView.frame = CGRectMake(cx - cover / 2, top, cover, cover);
    CGFloat y = top + cover + 14;
    sourceLabel.frame = CGRectMake(0, y, W, 18); y += 20;
    titleLabel.frame = CGRectMake(20, y, W - 40, 32); y += 34;
    artistLabel.frame = CGRectMake(20, y, W - 40, 26); y += 26;
    albumLabel.frame = CGRectMake(20, y, W - 40, 22); y += 32;
    CGFloat tw = W * 0.6f;
    track.frame = CGRectMake(cx - tw / 2, y, tw, 3);
    elapsedLabel.frame = CGRectMake(cx - tw / 2, y + 6, 80, 16);
    remainingLabel.frame = CGRectMake(cx + tw / 2 - 80, y + 6, 80, 16);
    idleLabel.frame = CGRectMake(0, top + cover / 2 - 12, W, 24);
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
  NSString* p = [FIDELIO_DIR stringByAppendingPathComponent:@"lockplugin.plist"];
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
  NSString* ap = [FIDELIO_DIR stringByAppendingPathComponent:@"active-source"];
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
    LPTrace([NSString stringWithFormat:@"refresh: pickSource='%@' haveContent=%d", src, (int)haveContent]);
    // B1.16.3: the base class may cache displayability; keep it in sync when our content appears/goes.
    if (haveContent != had) { @try { [self setViewCanBeDisplayed:haveContent]; } @catch (NSException* e) {} }

    if (haveContent) {
      NSString* path = [FIDELIO_DIR stringByAppendingPathComponent:[NSString stringWithFormat:@"status-%@.plist", source]];
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
  NSString* coverPath = [name length] ? [FIDELIO_DIR stringByAppendingPathComponent:name] : nil;
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
  @try { return !(keepScreenOn && haveContent); } @catch (NSException* e) { return YES; }
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
