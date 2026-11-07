#import "NowPlayingViewController.h"
#import <QuartzCore/QuartzCore.h>

// Layout for a 320x480 portrait screen with the status bar hidden and a 49 px tab bar: 431 px usable.
#define COVER_SIZE FIDELIO_COVER_PX  // FidelioStatus scales the cover to exactly this

@implementation NowPlayingViewController

- (id)initWithStatus:(FidelioStatus*)s {
  if ((self = [super initWithNibName:nil bundle:nil])) {
    status = s;
    self.title = @"Now Playing";
    self.tabBarItem = [[[UITabBarItem alloc] initWithTabBarSystemItem:UITabBarSystemItemMostViewed tag:0] autorelease];
    self.tabBarItem.title = @"Now Playing";
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
  NSString* title = [status string:@"title"];
  NSString* artist = [status string:@"artist"];
  NSString* album = [status string:@"album"];
  BOOL hasTrack = [title length] > 0;

  titleLabel.text = hasTrack ? title : [status string:@"device"];
  artistLabel.text = hasTrack ? artist : @"";
  albumLabel.text = hasTrack ? album : @"";

  UIImage* img = status.cover;
  if (coverView.image != (img ? img : placeholder)) coverView.image = img ? img : placeholder;

  long long dur = [status durationMs];
  long long pos = [status positionMsNow];
  progress.progress = dur > 0 ? (float)((double)pos / (double)dur) : 0.0f;
  elapsedLabel.text = hasTrack ? [FidelioStatus formatTime:pos] : @"";
  remainingLabel.text = (hasTrack && dur > 0) ? [NSString stringWithFormat:@"-%@", [FidelioStatus formatTime:dur - pos]] : @"";

  NSString* line;
  if ([st length] == 0) line = status.fileMissing ? @"Waiting for cspot (no status file)" : @"Waiting for Spotify";
  else if ([st isEqualToString:@"playing"]) line = @"Playing";
  else if ([st isEqualToString:@"paused"]) line = @"Paused";
  else if ([st isEqualToString:@"stopped"]) line = @"Stopped";
  else if ([st isEqualToString:@"connected"]) line = @"Connected - waiting for Spotify";
  else line = @"Waiting for Spotify";
  NSString* src = [status string:@"source"];
  if ([src length] && ([st isEqualToString:@"playing"] || [st isEqualToString:@"paused"]))
    line = [NSString stringWithFormat:@"%@ via %@", line, [src isEqualToString:@"airplay"] ? @"AirPlay" : ([src isEqualToString:@"spotify"] ? @"Spotify" : src)];
  if ([status fileAge] > 15 && ![st isEqualToString:@""]) line = [line stringByAppendingString:@" (stale)"];
  stateLabel.text = line;
}

@end
