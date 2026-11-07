#import "TechnicalViewController.h"

@implementation TechnicalViewController

- (id)initWithStatus:(FidelioStatus*)s {
  if ((self = [super initWithStyle:UITableViewStylePlain])) {
    status = s;
    self.title = @"Technical";
    self.tabBarItem = [[[UITabBarItem alloc] initWithTabBarSystemItem:UITabBarSystemItemMore tag:1] autorelease];
    self.tabBarItem.title = @"Technical";
    rows = [[NSArray alloc] init];
  }
  return self;
}

- (void)dealloc {
  [rows release];
  [super dealloc];
}

- (void)viewDidLoad {
  [super viewDidLoad];
  self.tableView.backgroundColor = [UIColor blackColor];
  self.tableView.separatorColor = [UIColor colorWithWhite:0.22f alpha:1];
  self.tableView.rowHeight = 34;
  self.tableView.allowsSelection = NO;
  [self update];
}

- (void)update {
  NSString* codec = [NSString stringWithFormat:@"%@ %lld kbps", [status string:@"codec"], [status integer:@"bitrate_kbps"]];
  NSString* tls = [status string:@"tls"];
  NSString* ap = [status string:@"ap"];
  NSString* country = [status string:@"country"];
  NSTimeInterval age = [status fileAge];
  NSString* ageStr = age < 0 ? @"no file" : [NSString stringWithFormat:@"%.0f s", age];
  NSString* rss = [status integer:@"rss_kb"] ? [NSString stringWithFormat:@"%lld KB", [status integer:@"rss_kb"]] : @"";
  NSArray* r = [NSArray arrayWithObjects:
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
  [self.tableView reloadData];
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
    cell.textLabel.font = [UIFont systemFontOfSize:15];
    cell.detailTextLabel.textColor = [UIColor whiteColor];
    cell.detailTextLabel.font = [UIFont boldSystemFontOfSize:15];
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
