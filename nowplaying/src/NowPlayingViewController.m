#import "NowPlayingViewController.h"
#import <QuartzCore/QuartzCore.h>

#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>

// Layout for a 320x480 portrait screen with the status bar hidden and a 49 px tab bar: 431 px usable.
#define COVER_SIZE FIDELIO_COVER_PX  // FidelioStatus scales the cover to exactly this

@implementation NowPlayingViewController

- (id)initWithStatus:(FidelioStatus*)s {
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
  UIView* v = [[UIView alloc] initWithFrame:CGRectMake(0, 0, 320, 431)];
  v.backgroundColor = [UIColor blackColor];
  v.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
  self.view = v;
  [v release];

  NSString* ph = [[NSBundle mainBundle] pathForResource:@"placeholder" ofType:@"png"];
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
  prevBtn = [self makeControl:0 action:@selector(prevTapped)];
  playBtn = [self makeControl:1 action:@selector(playTapped)];  // 1 = play glyph by default; update: swaps to pause
  nextBtn = [self makeControl:3 action:@selector(nextTapped)];
  [self layoutContent];

  [self update];
}

// B1.16.1: draw a transport glyph (0=prev |<<, 1=play >, 2=pause ||, 3=next >>|) ourselves, so both
// iOS 3.1.3 (plain) and iOS 5.1.1 (which turns U+25B6 into a blue emoji) render the same, and the
// disabled state is a visible dim. 36x30 white-on-clear.
+ (UIImage*)transportGlyph:(int)kind dim:(BOOL)dim {
  CGSize sz = CGSizeMake(36, 30);
  UIGraphicsBeginImageContext(sz);
  CGContextRef c = UIGraphicsGetCurrentContext();
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
  [b setImage:[[self class] transportGlyph:kind dim:NO] forState:UIControlStateNormal];
  [b setImage:[[self class] transportGlyph:kind dim:YES] forState:UIControlStateDisabled];
  [b addTarget:self action:sel forControlEvents:UIControlEventTouchUpInside];
  [self.view addSubview:b];
  return [b retain];  // ivar owns it; released in releaseSubviews
}

// Set the play/pause button's glyph (1=play, 2=pause) for both the normal and disabled states.
- (void)setPlayGlyph:(int)kind {
  [playBtn setImage:[[self class] transportGlyph:kind dim:NO] forState:UIControlStateNormal];
  [playBtn setImage:[[self class] transportGlyph:kind dim:YES] forState:UIControlStateDisabled];
}

// B1.16.1: lay out every subview from the view's REAL height (431 usable on the 320x480 iPhone frame
// with the 49 px tab bar), anchoring the bottom cluster to the height so the controls can never fall
// under the tab bar. Called from loadView and on every appearance / frame change.
- (void)viewWillAppear:(BOOL)animated { [super viewWillAppear:animated]; [self layoutContent]; }

- (void)layoutContent {
  CGFloat W = self.view.bounds.size.width, H = self.view.bounds.size.height;
  if (W < 1 || H < 1) return;
  coverView.frame = CGRectMake((W - COVER_SIZE) / 2, 4, COVER_SIZE, COVER_SIZE);
  CGFloat y = 4 + COVER_SIZE + 4;                    // just below the cover
  titleLabel.frame  = CGRectMake(12, y, W - 24, 30); y += 30;
  artistLabel.frame = CGRectMake(12, y, W - 24, 24); y += 24;
  albumLabel.frame  = CGRectMake(12, y, W - 24, 20);
  CGFloat btnH = 40, btnY = H - btnH - 5;            // the control row sits just above the tab bar
  CGFloat stateY = btnY - 18, timesY = stateY - 18, progY = timesY - 12;
  progress.frame       = CGRectMake(20, progY, W - 40, 9);
  elapsedLabel.frame   = CGRectMake(20, timesY, 110, 16);
  remainingLabel.frame = CGRectMake(W - 130, timesY, 110, 16);
  stateLabel.frame     = CGRectMake(14, stateY, W - 28, 16);
  CGFloat cx[3] = { W * 0.25f, W * 0.5f, W * 0.75f };
  prevBtn.frame = CGRectMake(cx[0] - 30, btnY, 60, btnH);
  playBtn.frame = CGRectMake(cx[1] - 30, btnY, 60, btnH);
  nextBtn.frame = CGRectMake(cx[2] - 30, btnY, 60, btnH);
  // Regression guard: nothing may extend past the usable height.
  CGFloat maxB = 0; UIView* worst = nil;
  for (UIView* v in self.view.subviews) { CGFloat b = CGRectGetMaxY(v.frame); if (b > maxB) { maxB = b; worst = v; } }
  if (maxB > H + 0.5f) NSLog(@"NowPlaying LAYOUT WARNING: a subview bottom (%.0f) exceeds the usable height (%.0f): %@", maxB, H, worst);
}

- (void)prevTapped { [self sendControl:5]; }   // PREV
- (void)nextTapped { [self sendControl:4]; }   // NEXT
- (void)playTapped { [self sendControl:3]; }   // TOGGLE

// Send one transport verb to audiod over its Unix socket (HELLO as the non-producing "control"
// source, then CONTROL). Short timeouts; never blocks the UI for long. Mirrors common/AudiodControl.
- (void)sendControl:(uint8_t)verb {
  NSString* sp = [FIDELIO_DIR stringByAppendingPathComponent:@"audiod.sock"];
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

- (void)update {
  NSString* st = [status state];
  // Only a live playing/paused state shows track data; anything else (idle, connected, stopped,
  // no/unreadable/stale status file) shows the placeholder tile and one status line.
  BOOL stale = status.fileMissing || status.fileUnreadable || [status fileAge] > FIDELIO_STALE_S;
  BOOL playing = [st isEqualToString:@"playing"], paused = [st isEqualToString:@"paused"];
  // audiod decides who owns the speaker: with it idle, a player that still claims "playing" (a
  // paused AirPlay sender streaming silence, a source that lost the arbitration) is not shown.
  BOOL active = !stale && (playing || paused) && ![status audiodIdle];

  if (active) {
    titleLabel.text = [status string:@"title"];
    artistLabel.text = [status string:@"artist"];
    albumLabel.text = [status string:@"album"];
    UIImage* img = status.cover;
    if (coverView.image != (img ? img : placeholder)) coverView.image = img ? img : placeholder;
    long long dur = [status durationMs];
    long long pos = [status positionMsNow];
    progress.progress = dur > 0 ? (float)((double)pos / (double)dur) : 0.0f;
    elapsedLabel.text = [FidelioStatus formatTime:pos];
    remainingLabel.text = dur > 0 ? [NSString stringWithFormat:@"-%@", [FidelioStatus formatTime:dur - pos]] : @"";
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
  if (status.fileMissing) line = @"Waiting for a player (no status file)";
  else if (status.fileUnreadable) line = @"Waiting for a player (unreadable status file)";
  else if (stale) line = @"Waiting for a player (status not updating)";
  else if (active) {
    NSString* src = [status activeSource];
    if (![src length]) src = [status string:@"source"];
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
