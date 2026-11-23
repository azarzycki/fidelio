#import "TechnicalViewController.h"
#import "NPRemoteControl.h"

#include <string.h>
#include <sys/stat.h>
#include <time.h>

// B1.16.13: the dock remote's key log (written by the DockRemote tweak in iapd, phone only).
#define NP_KEYLOG_PATH DOCKRECEIVER_DIR "/remote-keys.log"
#define NP_KEYLOG_POLL_S 0.5     // twice a second while the tab is visible
#define NP_KEYLOG_FRESH_S 3      // the newest press is highlighted this long after it was logged
enum { SECTION_KEYS = 0, SECTION_STATUS = 1 };

@implementation TechnicalViewController

- (id)initWithStatus:(NPStatus*)s {
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
  [self stopKeyPolling];
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
  self.view = [NPStatus columnViewForTable:table background:[UIColor blackColor]];
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
  table.rowHeight = [NPStatus isTablet] ? 44 : 30;
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
// B1.16.9: keep the window first responder, so the iPad's dock remote events have a responder.
// B1.16.13: the key log is polled while the tab is visible. The app delegate stops the poll when another
// tab is selected or the app resigns active (the sysroot's UIKit declares no viewWillDisappear:).
- (void)viewDidAppear:(BOOL)animated {
  [super viewDidAppear:animated];
  NPRemoteControlClaimFirstResponder(self.view);
  [self startKeyPolling];
}

// B1.18: the tablet rotates; the phone stays portrait as before.
- (BOOL)shouldAutorotateToInterfaceOrientation:(UIInterfaceOrientation)o { return [NPStatus allowsOrientation:o]; }

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
      [NSArray arrayWithObjects:@"App RSS", [NSString stringWithFormat:@"%lu KB", [NPStatus ownRssKB]], nil],
      [NSArray arrayWithObjects:@"Free memory", [NSString stringWithFormat:@"%lu KB", [NPStatus freeMemoryKB]], nil],
      [NSArray arrayWithObjects:@"Uptime", [NPStatus formatDuration:[NPStatus uptime]], nil],
      [NSArray arrayWithObjects:@"Wi-Fi IP", [NPStatus wifiAddress], nil],
      [NSArray arrayWithObjects:@"Battery", [NPStatus batteryString], nil],
      [NSArray arrayWithObjects:@"Status age", ageStr, nil],
      [NSArray arrayWithObjects:@"Device", [status string:@"device"], nil],
      [NSArray arrayWithObjects:@"Build", NP_APP_VERSION, nil],
      nil];
  [rows release];
  rows = [r retain];
  [table reloadData];
}

// ---- B1.16.13: the dock remote's key presses -------------------------------------------------------------

- (void)startKeyPolling {
  if (keyTimer) return;
  // The timer retains its target; -stopKeyPolling (from the app delegate, or dealloc) breaks that.
  keyTimer = [[NSTimer scheduledTimerWithTimeInterval:NP_KEYLOG_POLL_S target:self selector:@selector(pollKeys:)
                                             userInfo:nil repeats:YES] retain];
  [self pollKeys:nil];
}

- (void)stopKeyPolling {
  [keyTimer invalidate];
  [keyTimer release];
  keyTimer = nil;
}

// Cheap when nothing happened: one stat(); the file is read only when its size, date or inode changed.
- (void)pollKeys:(NSTimer*)t {
  BOOL changed = NO;
  struct stat st;
  if (stat(NP_KEYLOG_PATH, &st) != 0) {
    if (keyFileKnown || keyCount) changed = YES;
    keyFileKnown = NO;
    keyCount = 0;
  } else if (!keyFileKnown || st.st_mtime != keyMtime || st.st_size != keySize || st.st_ino != keyIno) {
    NPKeyEntry parsed[NP_KEYLOG_SHOWN];
    memset(parsed, 0, sizeof parsed);  // so that memcmp below compares only what was parsed
    int n = NPKeyLogRead(NP_KEYLOG_PATH, parsed, NP_KEYLOG_SHOWN);
    if (n != keyCount || (n > 0 && memcmp(parsed, keys, sizeof(NPKeyEntry) * n) != 0)) changed = YES;
    memset(keys, 0, sizeof keys);
    if (n > 0) memcpy(keys, parsed, sizeof(NPKeyEntry) * n);
    keyCount = n;
    keyFileKnown = YES;
    keyMtime = st.st_mtime;
    keySize = st.st_size;
    keyIno = st.st_ino;
  }
  long long age = keyCount ? (long long)time(NULL) - keys[0].time : -1000;
  BOOL fresh = keyCount > 0 && age >= -1 && age <= NP_KEYLOG_FRESH_S;
  if (fresh != keyFresh) changed = YES;
  keyFresh = fresh;
  if (changed) [table reloadData];
}

- (UITableViewCell*)keyCellFor:(UITableView*)tableView row:(NSInteger)r {
  BOOL tablet = [NPStatus isTablet];
  CGFloat fs = tablet ? 20 : 15;
  if (keyCount == 0) {
    static NSString* emptyId = @"keyempty";
    UITableViewCell* cell = [tableView dequeueReusableCellWithIdentifier:emptyId];
    if (!cell) {
      cell = [[[UITableViewCell alloc] initWithStyle:UITableViewCellStyleSubtitle reuseIdentifier:emptyId] autorelease];
      cell.backgroundColor = [UIColor blackColor];
      cell.selectionStyle = UITableViewCellSelectionStyleNone;
      cell.textLabel.textColor = [UIColor whiteColor];
      cell.textLabel.font = [UIFont boldSystemFontOfSize:fs];
      cell.detailTextLabel.textColor = [UIColor colorWithWhite:0.65f alpha:1];
      cell.detailTextLabel.font = [UIFont systemFontOfSize:tablet ? 17 : 13];
      cell.detailTextLabel.adjustsFontSizeToFitWidth = YES;
      cell.detailTextLabel.minimumFontSize = 9;
    }
    // One entry, on two lines (UILabel's numberOfLines is not in the sysroot's UIKit).
    cell.textLabel.text = @"No key seen yet.";
    cell.detailTextLabel.text = @"The dock remote tweak logs keys only while it is observing.";
    return cell;
  }
  static NSString* keyId = @"key";
  UITableViewCell* cell = [tableView dequeueReusableCellWithIdentifier:keyId];
  if (!cell) {
    cell = [[[UITableViewCell alloc] initWithStyle:UITableViewCellStyleValue1 reuseIdentifier:keyId] autorelease];
    cell.backgroundColor = [UIColor blackColor];
    cell.selectionStyle = UITableViewCellSelectionStyleNone;
    cell.detailTextLabel.adjustsFontSizeToFitWidth = YES;
    cell.detailTextLabel.minimumFontSize = 10;
  }
  const NPKeyEntry* k = &keys[r];
  char clock[16], words[NP_KEYLOG_NAME_MAX + 8];
  NPKeyClock(k->time, clock, sizeof clock);
  NPKeyWords(k->name, words, sizeof words);
  cell.textLabel.text = [NSString stringWithUTF8String:clock];
  NSString* w = [NSString stringWithUTF8String:words];
  cell.detailTextLabel.text = k->taken ? [w stringByAppendingString:@" (taken)"] : w;
  // The newest press stands out: bold and white, and green for a few seconds after it was logged.
  // Older ones are grey and regular weight.
  BOOL newest = r == 0;
  UIColor* green = [UIColor colorWithRed:0.3f green:1.0f blue:0.3f alpha:1];
  UIColor* c = newest ? (keyFresh ? green : [UIColor whiteColor]) : [UIColor colorWithWhite:0.6f alpha:1];
  cell.textLabel.textColor = c;
  cell.detailTextLabel.textColor = c;
  cell.textLabel.font = newest ? [UIFont boldSystemFontOfSize:fs] : [UIFont systemFontOfSize:fs];
  cell.detailTextLabel.font = newest ? [UIFont boldSystemFontOfSize:fs] : [UIFont systemFontOfSize:fs];
  return cell;
}

- (NSString*)tableView:(UITableView*)tableView titleForHeaderInSection:(NSInteger)section {
  return section == SECTION_KEYS ? @"Dock remote" : nil;
}

- (CGFloat)tableView:(UITableView*)tableView heightForRowAtIndexPath:(NSIndexPath*)indexPath {
  if (indexPath.section == SECTION_KEYS && keyCount == 0) return [NPStatus isTablet] ? 60 : 44;  // two lines
  return tableView.rowHeight;
}

// ---- the table -------------------------------------------------------------------------------------------

- (NSInteger)numberOfSectionsInTableView:(UITableView*)tableView {
  return 2;
}

- (NSInteger)tableView:(UITableView*)tableView numberOfRowsInSection:(NSInteger)section {
  if (section == SECTION_KEYS) return keyCount ? keyCount : 1;
  return [rows count];
}

- (UITableViewCell*)tableView:(UITableView*)tableView cellForRowAtIndexPath:(NSIndexPath*)indexPath {
  if (indexPath.section == SECTION_KEYS) return [self keyCellFor:tableView row:indexPath.row];
  static NSString* cellId = @"tech";
  UITableViewCell* cell = [tableView dequeueReusableCellWithIdentifier:cellId];
  if (!cell) {
    cell = [[[UITableViewCell alloc] initWithStyle:UITableViewCellStyleValue1 reuseIdentifier:cellId] autorelease];
    cell.backgroundColor = [UIColor blackColor];
    cell.textLabel.textColor = [UIColor colorWithWhite:0.65f alpha:1];
    CGFloat fs = [NPStatus isTablet] ? 20 : 15;  // B1.18: tablet type
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
