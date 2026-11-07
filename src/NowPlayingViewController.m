#import "NowPlayingViewController.h"
#import <QuartzCore/QuartzCore.h>

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
}

- (void)dealloc {
  [self releaseSubviews];
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
  progress.frame = CGRectMake(20, 372, 280, 9);
  [self.view addSubview:progress];

  elapsedLabel = [self makeLabel:CGRectMake(20, 384, 100, 20) font:[UIFont boldSystemFontOfSize:15] color:dim align:UITextAlignmentLeft];
  remainingLabel = [self makeLabel:CGRectMake(200, 384, 100, 20) font:[UIFont boldSystemFontOfSize:15] color:dim align:UITextAlignmentRight];
  stateLabel = [self makeLabel:CGRectMake(20, 406, 280, 20) font:[UIFont systemFontOfSize:14] color:[UIColor colorWithRed:0.45f green:0.80f blue:0.55f alpha:1] align:UITextAlignmentCenter];

  [self update];
}

- (void)update {
  NSString* st = [status state];
  // Only a live playing/paused state shows track data; anything else (idle, connected, stopped,
  // no/unreadable/stale status file) shows the placeholder tile and one status line.
  BOOL stale = status.fileMissing || status.fileUnreadable || [status fileAge] > FIDELIO_STALE_S;
  BOOL playing = [st isEqualToString:@"playing"], paused = [st isEqualToString:@"paused"];
  BOOL active = !stale && (playing || paused);

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
  if (status.fileMissing) line = @"Waiting for cspot (no status file)";
  else if (status.fileUnreadable) line = @"Waiting for cspot (unreadable status file)";
  else if (stale) line = @"Waiting for cspot (status not updating)";
  else if (active) {
    NSString* src = [status string:@"source"];
    NSString* via = [src isEqualToString:@"airplay"] ? @"AirPlay" : ([src isEqualToString:@"spotify"] ? @"Spotify" : src);
    line = playing ? @"Playing" : @"Paused";
    if ([via length]) line = [NSString stringWithFormat:@"%@ via %@", line, via];
  }
  else if ([st isEqualToString:@"stopped"]) line = @"Stopped";
  else line = @"Waiting for Spotify";
  stateLabel.text = line;
}

@end
