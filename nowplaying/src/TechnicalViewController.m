#import "TechnicalViewController.h"

@implementation TechnicalViewController

- (id)initWithStatus:(FidelioStatus*)s {
  if ((self = [super initWithNibName:nil bundle:nil])) {
    status = s;
    self.title = @"Technical";
    UIImage* icon = [[UIImage alloc] initWithContentsOfFile:[[NSBundle mainBundle] pathForResource:@"tab_technical" ofType:@"png"]];
    self.tabBarItem = [[[UITabBarItem alloc] initWithTitle:@"Technical" image:icon tag:1] autorelease];
    [icon release];
    rows = [[NSArray alloc] init];
  }
  return self;
}

- (void)dealloc {
  [rows release];
  [table release];
  [super dealloc];
}

- (void)loadView {
  CGSize screen = [UIScreen mainScreen].bounds.size;
  table = [[UITableView alloc] initWithFrame:CGRectMake(0, 0, screen.width, screen.height - 49) style:UITableViewStylePlain];
  table.dataSource = self;
  table.delegate = self;
  table.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
  self.view = [FidelioStatus columnViewForTable:table background:[UIColor blackColor]];
}

- (void)viewDidUnload {  // a memory warning purged the view: let the table go with it (loadView makes a new one)
  [super viewDidUnload];
  [table release];
  table = nil;
}

- (void)viewDidLoad {
  [super viewDidLoad];
  table.backgroundColor = [UIColor blackColor];
  table.separatorColor = [UIColor colorWithWhite:0.22f alpha:1];
  table.rowHeight = [FidelioStatus isTablet] ? 44 : 30;
  table.allowsSelection = NO;
  // The row list grows over time (the last one, Build, was clipped behind the tab bar on the iPad).
  // A bottom inset the height of the tab bar lets the final rows scroll clear on any screen instead
  // of depending on an exact row-count-times-height fit.
  table.scrollEnabled = YES;
  table.contentInset = UIEdgeInsetsMake(0, 0, 56, 0);
  table.scrollIndicatorInsets = table.contentInset;
  [self update];
}

- (void)viewWillAppear:(BOOL)animated { [super viewWillAppear:animated]; [self update]; }

// B1.18: the tablet rotates; the phone stays portrait as before.
- (BOOL)shouldAutorotateToInterfaceOrientation:(UIInterfaceOrientation)o { return [FidelioStatus allowsOrientation:o]; }

- (void)update {
  NSString* codec = [NSString stringWithFormat:@"%@ %lld kbps", [status string:@"codec"], [status integer:@"bitrate_kbps"]];
  NSString* tls = [status string:@"tls"];
  NSString* ap = [status string:@"ap"];
  NSString* country = [status string:@"country"];
  NSTimeInterval age = [status fileAge];
  NSString* ageStr = age < 0 ? @"no file" : [NSString stringWithFormat:@"%.0f s", age];
  NSString* rss = [status integer:@"rss_kb"] ? [NSString stringWithFormat:@"%lld KB", [status integer:@"rss_kb"]] : @"";
  NSArray* r = [NSArray arrayWithObjects:
      [NSArray arrayWithObjects:@"Source", [status string:@"source"], nil],
      [NSArray arrayWithObjects:@"State", [status state], nil],
      [NSArray arrayWithObjects:@"Codec", [status dict] ? codec : @"", nil],
      [NSArray arrayWithObjects:@"TLS", [tls length] ? tls : @"(none yet)", nil],
      [NSArray arrayWithObjects:@"Access point", ap, nil],
      [NSArray arrayWithObjects:@"Country", country, nil],
      [NSArray arrayWithObjects:@"cspot RSS", rss, nil],
      [NSArray arrayWithObjects:@"App RSS", [NSString stringWithFormat:@"%lu KB", [FidelioStatus ownRssKB]], nil],
      [NSArray arrayWithObjects:@"Free memory", [NSString stringWithFormat:@"%lu KB", [FidelioStatus freeMemoryKB]], nil],
      [NSArray arrayWithObjects:@"Uptime", [FidelioStatus formatDuration:[FidelioStatus uptime]], nil],
      [NSArray arrayWithObjects:@"Wi-Fi IP", [FidelioStatus wifiAddress], nil],
      [NSArray arrayWithObjects:@"Battery", [FidelioStatus batteryString], nil],
      [NSArray arrayWithObjects:@"Status age", ageStr, nil],
      [NSArray arrayWithObjects:@"Device", [status string:@"device"], nil],
      [NSArray arrayWithObjects:@"Build", FIDELIO_APP_VERSION, nil],
      nil];
  [rows release];
  rows = [r retain];
  [table reloadData];
}

- (NSInteger)numberOfSectionsInTableView:(UITableView*)tableView {
  return 1;
}

- (NSInteger)tableView:(UITableView*)tableView numberOfRowsInSection:(NSInteger)section {
  return [rows count];
}

- (UITableViewCell*)tableView:(UITableView*)tableView cellForRowAtIndexPath:(NSIndexPath*)indexPath {
  static NSString* cellId = @"tech";
  UITableViewCell* cell = [tableView dequeueReusableCellWithIdentifier:cellId];
  if (!cell) {
    cell = [[[UITableViewCell alloc] initWithStyle:UITableViewCellStyleValue1 reuseIdentifier:cellId] autorelease];
    cell.backgroundColor = [UIColor blackColor];
    cell.textLabel.textColor = [UIColor colorWithWhite:0.65f alpha:1];
    CGFloat fs = [FidelioStatus isTablet] ? 20 : 15;  // B1.18: tablet type
    cell.textLabel.font = [UIFont systemFontOfSize:fs];
    cell.detailTextLabel.textColor = [UIColor whiteColor];
    cell.detailTextLabel.font = [UIFont boldSystemFontOfSize:fs];
    cell.detailTextLabel.adjustsFontSizeToFitWidth = YES;
    cell.detailTextLabel.minimumFontSize = 10;
    cell.selectionStyle = UITableViewCellSelectionStyleNone;
  }
  NSArray* row = [rows objectAtIndex:indexPath.row];
  cell.textLabel.text = [row objectAtIndex:0];
  cell.detailTextLabel.text = [row objectAtIndex:1];
  return cell;
}

- (void)tableView:(UITableView*)tableView willDisplayCell:(UITableViewCell*)cell forRowAtIndexPath:(NSIndexPath*)indexPath {
  cell.backgroundColor = [UIColor blackColor];  // UITableView resets it on plain style
}

@end
