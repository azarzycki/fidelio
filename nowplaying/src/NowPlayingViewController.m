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

  coverView = [[UIImageView alloc] initWithFrame:CGRectMake((320 - COVER_SIZE) / 2, 12, COVER_SIZE, COVER_SIZE)];
  coverView.contentMode = UIViewContentModeScaleAspectFill;
  coverView.layer.cornerRadius = 10.0f;
  coverView.layer.masksToBounds = YES;
  coverView.image = placeholder;
  [self.view addSubview:coverView];

  UIColor* dim = [UIColor colorWithWhite:0.62f alpha:1.0f];
  titleLabel = [self makeLabel:CGRectMake(12, 276, 296, 32) font:[UIFont boldSystemFontOfSize:26] color:[UIColor whiteColor] align:UITextAlignmentCenter];
  artistLabel = [self makeLabel:CGRectMake(12, 308, 296, 26) font:[UIFont systemFontOfSize:20] color:[UIColor colorWithWhite:0.85f alpha:1] align:UITextAlignmentCenter];
  albumLabel = [self makeLabel:CGRectMake(12, 334, 296, 22) font:[UIFont systemFontOfSize:16] color:dim align:UITextAlignmentCenter];

  progress = [[UIProgressView alloc] initWithProgressViewStyle:UIProgressViewStyleDefault];
  progress.frame = CGRectMake(20, 360, 280, 9);
  [self.view addSubview:progress];

  elapsedLabel = [self makeLabel:CGRectMake(20, 370, 100, 18) font:[UIFont boldSystemFontOfSize:15] color:dim align:UITextAlignmentLeft];
  remainingLabel = [self makeLabel:CGRectMake(200, 370, 100, 18) font:[UIFont boldSystemFontOfSize:15] color:dim align:UITextAlignmentRight];
  stateLabel = [self makeLabel:CGRectMake(20, 390, 280, 16) font:[UIFont systemFontOfSize:13] color:[UIColor colorWithRed:0.45f green:0.80f blue:0.55f alpha:1] align:UITextAlignmentCenter];

  // B1.16: transport controls (previous / play-pause / next) driving the active source via audiod.
  prevBtn = [self makeControl:@"\u25C0\u25C0" x:40 action:@selector(prevTapped)];   // |< prev
  playBtn = [self makeControl:@"\u25B6" x:140 action:@selector(playTapped)];          // play/pause
  nextBtn = [self makeControl:@"\u25B6\u25B6" x:240 action:@selector(nextTapped)];   // next >|

  [self update];
}

- (UIButton*)makeControl:(NSString*)glyph x:(CGFloat)cx action:(SEL)sel {
  UIButton* b = [UIButton buttonWithType:UIButtonTypeCustom];
  b.frame = CGRectMake(cx - 32, 408, 64, 40);
  [b setTitle:glyph forState:UIControlStateNormal];
  b.titleLabel.font = [UIFont systemFontOfSize:26];
  [b setTitleColor:[UIColor whiteColor] forState:UIControlStateNormal];
  [b setTitleColor:[UIColor colorWithWhite:0.30f alpha:1] forState:UIControlStateDisabled];
  [b addTarget:self action:sel forControlEvents:UIControlEventTouchUpInside];
  [self.view addSubview:b];
  return [b retain];  // ivar owns it; released in releaseSubviews
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
  [playBtn setTitle:(playing ? @"\u25AE\u25AE" : @"\u25B6") forState:UIControlStateNormal];
}

- (void)showNotice:(NSString*)text forSeconds:(NSTimeInterval)seconds {
  [notice release];
  notice = [text copy];
  noticeUntil = CFAbsoluteTimeGetCurrent() + seconds;
  if (stateLabel) [self update];
}

@end
