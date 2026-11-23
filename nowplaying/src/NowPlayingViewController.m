#import "NowPlayingViewController.h"
#import "NPRemoteControl.h"
#import <QuartzCore/QuartzCore.h>

#include "NPLayout.h"

#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>

// B1.18: the layout comes from NPLayout (plain C, host-tested): the phone keeps the B1.16.1 frames
// (320x431 usable with the status bar hidden and the 49 px tab bar); the tablet gets a 600 pt cover,
// larger type and 60+ pt controls, stacked in portrait and side by side in landscape.

static CGRect CG(NPRect r) { return CGRectMake(r.x, r.y, r.w, r.h); }

@implementation NowPlayingViewController

- (id)initWithStatus:(NPStatus*)s {
  if ((self = [super initWithNibName:nil bundle:nil])) {
    status = s;
    self.title = @"Now Playing";
    // A system item (initWithTabBarSystemItem:) keeps its system title, so use our own 30 px glyph.
    UIImage* icon = [[UIImage alloc] initWithContentsOfFile:[[NSBundle mainBundle] pathForResource:@"tab_nowplaying" ofType:@"png"]];
    self.tabBarItem = [[[UITabBarItem alloc] initWithTitle:@"Now Playing" image:icon tag:0] autorelease];
    [icon release];
  }
  return self;
}

- (void)releaseSubviews {
  [coverView release];
  coverView = nil;
  [placeholder release];
  placeholder = nil;
  [titleLabel release];
  titleLabel = nil;
  [artistLabel release];
  artistLabel = nil;
  [albumLabel release];
  albumLabel = nil;
  [progress release];
  progress = nil;
  [elapsedLabel release];
  elapsedLabel = nil;
  [remainingLabel release];
  remainingLabel = nil;
  [stateLabel release];
  stateLabel = nil;
  [prevBtn release]; prevBtn = nil;
  [playBtn release]; playBtn = nil;
  [nextBtn release]; nextBtn = nil;
}

- (void)dealloc {
  [self releaseSubviews];
  [notice release];
  [super dealloc];
}

// A memory warning while the Technical tab is showing purges this (offscreen) view; -loadView
// then runs again when the tab is reselected, so the old subviews must be let go here or they
// leak (placeholder bitmap, image view, labels) every time it happens.
- (void)viewDidUnload {
  [super viewDidUnload];
  [self releaseSubviews];
}

- (UILabel*)makeLabel:(CGRect)frame font:(UIFont*)font color:(UIColor*)color align:(UITextAlignment)align {
  UILabel* l = [[UILabel alloc] initWithFrame:frame];
  l.font = font;
  l.textColor = color;
  l.backgroundColor = [UIColor clearColor];
  l.textAlignment = align;
  l.lineBreakMode = UILineBreakModeTailTruncation;
  [self.view addSubview:l];
  return l;
}

- (void)loadView {
  CGSize screen = [UIScreen mainScreen].bounds.size;  // the tab bar controller sets the real frame later
  UIView* v = [[UIView alloc] initWithFrame:CGRectMake(0, 0, screen.width, screen.height - 49)];
  v.backgroundColor = [UIColor blackColor];
  v.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
  self.view = v;
  [v release];

  NSString* ph = [[NSBundle mainBundle] pathForResource:([NPStatus isTablet] ? @"placeholder_600" : @"placeholder") ofType:@"png"];
  placeholder = [[UIImage alloc] initWithContentsOfFile:ph];

  coverView = [[UIImageView alloc] initWithFrame:CGRectZero];
  coverView.contentMode = UIViewContentModeScaleAspectFill;
  coverView.layer.cornerRadius = 10.0f;
  coverView.layer.masksToBounds = YES;
  coverView.image = placeholder;
  [self.view addSubview:coverView];

  UIColor* dim = [UIColor colorWithWhite:0.62f alpha:1.0f];
  titleLabel = [self makeLabel:CGRectZero font:[UIFont boldSystemFontOfSize:26] color:[UIColor whiteColor] align:UITextAlignmentCenter];
  artistLabel = [self makeLabel:CGRectZero font:[UIFont systemFontOfSize:20] color:[UIColor colorWithWhite:0.85f alpha:1] align:UITextAlignmentCenter];
  albumLabel = [self makeLabel:CGRectZero font:[UIFont systemFontOfSize:16] color:dim align:UITextAlignmentCenter];

  progress = [[UIProgressView alloc] initWithProgressViewStyle:UIProgressViewStyleDefault];
  [self.view addSubview:progress];

  elapsedLabel = [self makeLabel:CGRectZero font:[UIFont boldSystemFontOfSize:15] color:dim align:UITextAlignmentLeft];
  remainingLabel = [self makeLabel:CGRectZero font:[UIFont boldSystemFontOfSize:15] color:dim align:UITextAlignmentRight];
  stateLabel = [self makeLabel:CGRectZero font:[UIFont systemFontOfSize:13] color:[UIColor colorWithRed:0.45f green:0.80f blue:0.55f alpha:1] align:UITextAlignmentCenter];

  // B1.16/B1.16.1: transport controls (previous / play-pause / next), drawn as custom glyphs so iOS 5
  // does not render U+25B6 as a blue emoji box; the active source is driven via audiod.
  glyphScale = 1;
  titleFontSize = 0;
  playGlyph = 1;
  prevBtn = [self makeControl:0 action:@selector(prevTapped)];
  playBtn = [self makeControl:1 action:@selector(playTapped)];  // 1 = play glyph by default; update: swaps to pause
  nextBtn = [self makeControl:3 action:@selector(nextTapped)];
  [self layoutContent];

  [self update];
}

// B1.16.1: draw a transport glyph (0=prev |<<, 1=play >, 2=pause ||, 3=next >>|) ourselves, so both
// iOS 3.1.3 (plain) and iOS 5.1.1 (which turns U+25B6 into a blue emoji) render the same, and the
// disabled state is a visible dim. 36x30 white-on-clear, times `scale` (B1.18: 2 on the tablet).
+ (UIImage*)transportGlyph:(int)kind dim:(BOOL)dim scale:(CGFloat)scale {
  CGSize sz = CGSizeMake(36 * scale, 30 * scale);
  UIGraphicsBeginImageContext(sz);
  CGContextRef c = UIGraphicsGetCurrentContext();
  CGContextScaleCTM(c, scale, scale);
  CGFloat g = dim ? 0.32f : 1.0f;
  CGContextSetRGBFillColor(c, g, g, g, 1);
  CGFloat midY = 15, h = 18, half = h / 2;         // shapes ~18 px tall, vertically centered
  CGFloat tw = 11;                                  // triangle width
  if (kind == 1) {  // play: one right triangle
    CGContextMoveToPoint(c, 13, midY - half); CGContextAddLineToPoint(c, 13 + tw + 2, midY); CGContextAddLineToPoint(c, 13, midY + half); CGContextClosePath(c); CGContextFillPath(c);
  } else if (kind == 2) {  // pause: two bars
    CGContextFillRect(c, CGRectMake(12, midY - half, 4, h));
    CGContextFillRect(c, CGRectMake(20, midY - half, 4, h));
  } else {  // prev (0) / next (3): two triangles + a bar on the trailing side
    BOOL left = (kind == 0);
    CGFloat x0 = 6;  // left edge of the first triangle
    for (int i = 0; i < 2; i++) {
      CGFloat x = x0 + i * (tw + 1);
      if (left) { CGContextMoveToPoint(c, x + tw, midY - half); CGContextAddLineToPoint(c, x, midY); CGContextAddLineToPoint(c, x + tw, midY + half); }
      else      { CGContextMoveToPoint(c, x, midY - half); CGContextAddLineToPoint(c, x + tw, midY); CGContextAddLineToPoint(c, x, midY + half); }
      CGContextClosePath(c); CGContextFillPath(c);
    }
    CGFloat barX = left ? 4 : (x0 + 2 * (tw + 1));  // the little bar on the leading(prev)/trailing(next) edge
    CGContextFillRect(c, CGRectMake(barX, midY - half, 3, h));
  }
  UIImage* img = UIGraphicsGetImageFromCurrentImageContext();
  UIGraphicsEndImageContext();
  return img;
}

- (UIButton*)makeControl:(int)kind action:(SEL)sel {
  UIButton* b = [UIButton buttonWithType:UIButtonTypeCustom];
  b.tag = kind;  // the glyph kind, so a scale change can redraw it
  [b setImage:[[self class] transportGlyph:kind dim:NO scale:glyphScale] forState:UIControlStateNormal];
  [b setImage:[[self class] transportGlyph:kind dim:YES scale:glyphScale] forState:UIControlStateDisabled];
  [b addTarget:self action:sel forControlEvents:UIControlEventTouchUpInside];
  [self.view addSubview:b];
  return [b retain];  // ivar owns it; released in releaseSubviews
}

// Set the play/pause button's glyph (1=play, 2=pause) for both the normal and disabled states.
- (void)setPlayGlyph:(int)kind {
  if (kind == playGlyph && [playBtn imageForState:UIControlStateNormal]) return;  // nothing to redraw (update runs at 1 Hz)
  playGlyph = kind;
  [playBtn setImage:[[self class] transportGlyph:kind dim:NO scale:glyphScale] forState:UIControlStateNormal];
  [playBtn setImage:[[self class] transportGlyph:kind dim:YES scale:glyphScale] forState:UIControlStateDisabled];
}

// B1.18: the tablet rotates (the dock may hold it either way); the phone stays portrait as before.
- (BOOL)shouldAutorotateToInterfaceOrientation:(UIInterfaceOrientation)o { return [NPStatus allowsOrientation:o]; }
- (void)willAnimateRotationToInterfaceOrientation:(UIInterfaceOrientation)o duration:(NSTimeInterval)d { [self layoutContent]; }
- (void)didRotateFromInterfaceOrientation:(UIInterfaceOrientation)o { [self layoutContent]; }

// B1.16.1: lay out every subview from the view's REAL size, anchoring the bottom cluster to the height so
// the controls can never fall under the tab bar. B1.18: the frames come from NPLayoutCompute. Called
// from loadView, on every appearance and on rotation.
- (void)viewWillAppear:(BOOL)animated { [super viewWillAppear:animated]; [self layoutContent]; }

- (void)layoutContent {
  CGFloat W = self.view.bounds.size.width, H = self.view.bounds.size.height;
  if (W < 1 || H < 1) return;
  NPLayout L;
  NPLayoutCompute(W, H, [NPStatus isTablet] ? 1 : 0, &L);
  coverView.frame = CG(L.cover);
  coverView.layer.cornerRadius = L.tablet ? 16.0f : 10.0f;
  titleLabel.frame = CG(L.title);
  artistLabel.frame = CG(L.artist);
  albumLabel.frame = CG(L.album);
  progress.frame = CG(L.progress);
  elapsedLabel.frame = CG(L.elapsed);
  remainingLabel.frame = CG(L.remaining);
  stateLabel.frame = CG(L.state);
  prevBtn.frame = CG(L.prev);
  playBtn.frame = CG(L.play);
  nextBtn.frame = CG(L.next);
  if (L.titleFont != titleFontSize) {  // fonts change only between the phone and the tablet's two layouts
    titleFontSize = L.titleFont;
    titleLabel.font = [UIFont boldSystemFontOfSize:L.titleFont];
    artistLabel.font = [UIFont systemFontOfSize:L.artistFont];
    albumLabel.font = [UIFont systemFontOfSize:L.albumFont];
    elapsedLabel.font = [UIFont boldSystemFontOfSize:L.timeFont];
    remainingLabel.font = [UIFont boldSystemFontOfSize:L.timeFont];
    stateLabel.font = [UIFont systemFontOfSize:L.stateFont];
    // the side-by-side column is left-aligned under the title; the stacked layouts are centred
    UITextAlignment al = L.sideBySide ? UITextAlignmentLeft : UITextAlignmentCenter;
    titleLabel.textAlignment = al; artistLabel.textAlignment = al; albumLabel.textAlignment = al; stateLabel.textAlignment = al;
  }
  if (L.glyphScale != glyphScale) {
    glyphScale = L.glyphScale;
    UIButton* bs[2] = { prevBtn, nextBtn };
    for (int i = 0; i < 2; i++) {
      [bs[i] setImage:[[self class] transportGlyph:bs[i].tag dim:NO scale:glyphScale] forState:UIControlStateNormal];
      [bs[i] setImage:[[self class] transportGlyph:bs[i].tag dim:YES scale:glyphScale] forState:UIControlStateDisabled];
    }
    [playBtn setImage:nil forState:UIControlStateNormal];  // force setPlayGlyph to redraw at the new scale
    [self setPlayGlyph:playGlyph];
  }
  // Regression guard: nothing may extend past the usable height (the host layout test checks the same).
  CGFloat maxB = 0; UIView* worst = nil;
  for (UIView* v in self.view.subviews) { CGFloat bt = CGRectGetMaxY(v.frame); if (bt > maxB) { maxB = bt; worst = v; } }
  if (maxB > H + 0.5f) NSLog(@"NowPlaying LAYOUT WARNING: a subview bottom (%.0f) exceeds the usable height (%.0f): %@", maxB, H, worst);
}

- (void)prevTapped { [self sendControl:5]; }   // PREV
- (void)nextTapped { [self sendControl:4]; }   // NEXT
- (void)playTapped { [self sendControl:3]; }   // TOGGLE

// Send one transport verb to audiod (B1.16.9: the socket code moved to NPRemoteControl.m, shared with
// the iPad's dock remote).
- (void)sendControl:(uint8_t)verb { NPSendControl(verb); }

// B1.16.9: keep the window first responder, so the iPad's dock remote events have a responder.
- (void)viewDidAppear:(BOOL)animated { [super viewDidAppear:animated]; NPRemoteControlClaimFirstResponder(self.view); }

- (void)update {
  NSString* st = [status state];
  // Only a live playing/paused state shows track data; anything else (idle, connected, stopped,
  // no/unreadable/stale status file) shows the placeholder tile and one status line.
  BOOL stale = status.fileMissing || status.fileUnreadable || [status fileAge] > NP_STALE_S;
  BOOL playing = [st isEqualToString:@"playing"], paused = [st isEqualToString:@"paused"];
  // audiod decides who owns the speaker: with it idle, a player that still claims "playing" (a
  // paused AirPlay sender streaming silence, a source that lost the arbitration) is not shown.
  BOOL active = !stale && (playing || paused) && ![status audiodIdle];
  // B1.17: the data directory is missing, or nothing in it is fresh: say so instead of an empty screen.
  NPDaemons daemons = [status daemons];
  if (daemons != NP_DAEMONS_RUNNING) active = NO;
  // B1.16.16: a paused source that is still the dock remote's target keeps its track on screen; audiod
  // plays nothing, so it reads "Paused" whatever the player's file says (a silent gap reads "playing").
  if (active && [status pausedTarget]) { playing = NO; paused = YES; }

  if (active) {
    titleLabel.text = [status string:@"title"];
    artistLabel.text = [status string:@"artist"];
    albumLabel.text = [status string:@"album"];
    UIImage* img = status.cover;
    if (coverView.image != (img ? img : placeholder)) coverView.image = img ? img : placeholder;
    long long dur = [status durationMs];
    long long pos = [status positionMsNow];
    progress.progress = dur > 0 ? (float)((double)pos / (double)dur) : 0.0f;
    elapsedLabel.text = [NPStatus formatTime:pos];
    remainingLabel.text = dur > 0 ? [NSString stringWithFormat:@"-%@", [NPStatus formatTime:dur - pos]] : @"";
  } else if (daemons != NP_DAEMONS_RUNNING) {
    titleLabel.text = @"Daemons not running";
    artistLabel.text = daemons == NP_DAEMONS_NO_DIR ? @"No directory " DOCKRECEIVER_DIR
                                                    : @"Nothing in " DOCKRECEIVER_DIR " is being updated";
    albumLabel.text = @"audiod, Spotify and AirPlay are not running";
    if (coverView.image != placeholder) coverView.image = placeholder;
    progress.progress = 0.0f;
    elapsedLabel.text = @"";
    remainingLabel.text = @"";
  } else {
    titleLabel.text = @"";
    artistLabel.text = @"";
    albumLabel.text = @"";
    if (coverView.image != placeholder) coverView.image = placeholder;
    progress.progress = 0.0f;
    elapsedLabel.text = @"";
    remainingLabel.text = @"";
  }

  NSString* line;
  if (daemons != NP_DAEMONS_RUNNING) line = @"Daemons not running";
  else if (status.fileMissing) line = @"Waiting for a player (no status file)";
  else if (status.fileUnreadable) line = @"Waiting for a player (unreadable status file)";
  else if (stale) line = @"Waiting for a player (status not updating)";
  else if (active) {
    NSString* src = [status pausedTarget] ? [status shownSource] : [status activeSource];
    if (![src length] || [src isEqualToString:@"none"]) src = [status string:@"source"];
    NSString* via = [src isEqualToString:@"airplay"] ? @"AirPlay" : ([src isEqualToString:@"spotify"] ? @"Spotify" : src);
    line = playing ? @"Playing" : @"Paused";
    if ([via length]) line = [NSString stringWithFormat:@"%@ via %@", line, via];
  }
  else if ([status audiodIdle]) line = @"Idle";
  else if ([st isEqualToString:@"stopped"]) line = @"Stopped";
  else line = @"Waiting for Spotify or AirPlay";
  if (notice && CFAbsoluteTimeGetCurrent() < noticeUntil) line = notice;
  stateLabel.text = line;

  // B1.16: the transport controls are usable only while a source owns the output; the play/pause
  // glyph follows the real state. (audiod still resolves "which source"; the app just sends a verb.)
  prevBtn.enabled = active; nextBtn.enabled = active; playBtn.enabled = active;
  [self setPlayGlyph:(playing ? 2 : 1)];  // pause glyph while playing, play glyph otherwise
}

- (void)showNotice:(NSString*)text forSeconds:(NSTimeInterval)seconds {
  [notice release];
  notice = [text copy];
  noticeUntil = CFAbsoluteTimeGetCurrent() + seconds;
  if (stateLabel) [self update];
}

@end
