#import "SettingsViewController.h"

#import "FidelioStatus.h"  // FIDELIO_DIR
#import "EnumPickerViewController.h"
#import "StringEditorViewController.h"
#import "NPEventProbe.h"

#include <spawn.h>
#include <sys/wait.h>
#include <math.h>
extern char** environ;

static NSString* const kDaemons[] = { @"audiod", @"cspot", @"raop" };
static NSString* ConfPath(void) { return [FIDELIO_DIR stringByAppendingPathComponent:@"settings.conf"]; }
static NSString* ManifestPath(NSString* d) { return [FIDELIO_DIR stringByAppendingPathComponent:[NSString stringWithFormat:@"settings-%@.manifest.plist", d]]; }

// Group order across daemons; groups not listed go last in first-seen order.
static NSInteger GroupRank(NSString* g) {
  NSArray* order = [NSArray arrayWithObjects:@"Volume", @"Source switching", @"Controls", @"AirPlay", @"Spotify", @"Display", @"Advanced", nil];
  NSUInteger i = [order indexOfObject:g];
  return i == NSNotFound ? 100 : (NSInteger)i;
}

// C comparator (iOS 3.1 has no block-based sortUsingComparator:): rank first, then name.
static NSInteger GroupCompare(id a, id b, void* ctx) {
  NSInteger ra = GroupRank(a), rb = GroupRank(b);
  if (ra < rb) return NSOrderedAscending;
  if (ra > rb) return NSOrderedDescending;
  return [(NSString*)a compare:(NSString*)b];
}

@implementation SettingsViewController

- (id)init {
  if ((self = [super init])) {
    self.title = @"Settings";
    self.tabBarItem = [[[UITabBarItem alloc] initWithTitle:@"Settings" image:nil tag:2] autorelease];
    sections = [[NSMutableArray alloc] init];
    missing = [[NSMutableArray alloc] init];
  }
  return self;
}

- (void)loadView {
  table = [[UITableView alloc] initWithFrame:[UIScreen mainScreen].applicationFrame style:UITableViewStyleGrouped];
  table.dataSource = self;
  table.delegate = self;
  table.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
  self.view = table;
}

- (void)viewWillAppear:(BOOL)animated { [super viewWillAppear:animated]; [self reload]; }

- (void)update { [self reload]; }

// (Re)load the manifests and rebuild the grouped sections.
- (void)reload {
  [sections removeAllObjects];
  [missing removeAllObjects];
  NSMutableDictionary* byGroup = [NSMutableDictionary dictionary];  // group -> NSMutableArray of options
  NSMutableArray* groupOrder = [NSMutableArray array];
  for (int i = 0; i < 3; i++) {
    NSString* d = kDaemons[i];
    NSDictionary* man = [NSDictionary dictionaryWithContentsOfFile:ManifestPath(d)];
    if (![man objectForKey:@"options"]) { [missing addObject:d]; continue; }
    for (NSDictionary* opt in [man objectForKey:@"options"]) {
      NSString* g = [opt objectForKey:@"group"]; if (![g isKindOfClass:[NSString class]]) g = @"Other";
      if ([[opt objectForKey:@"advanced"] boolValue] && !showAdvanced) continue;
      NSMutableArray* arr = [byGroup objectForKey:g];
      if (!arr) { arr = [NSMutableArray array]; [byGroup setObject:arr forKey:g]; [groupOrder addObject:g]; }
      NSMutableDictionary* o = [[opt mutableCopy] autorelease];
      [o setObject:d forKey:@"daemon"];
      [arr addObject:o];
    }
  }
  [groupOrder sortUsingFunction:GroupCompare context:NULL];
  for (NSString* g in groupOrder)
    [sections addObject:[NSDictionary dictionaryWithObjectsAndKeys:g, @"title", [byGroup objectForKey:g], @"options", nil]];
  [table reloadData];
}

- (NSInteger)numberOfSectionsInTableView:(UITableView*)t { return (NSInteger)[sections count] + 1; }  // +1: the Advanced toggle / status

- (NSInteger)tableView:(UITableView*)t numberOfRowsInSection:(NSInteger)s {
  if (s == (NSInteger)[sections count]) return 2 + (NSInteger)[missing count];  // Advanced + probe switches + any "not running"
  return (NSInteger)[[[sections objectAtIndex:s] objectForKey:@"options"] count];
}

- (NSString*)tableView:(UITableView*)t titleForHeaderInSection:(NSInteger)s {
  if (s == (NSInteger)[sections count]) return @"";
  return [[sections objectAtIndex:s] objectForKey:@"title"];
}

- (NSString*)tableView:(UITableView*)t titleForFooterInSection:(NSInteger)s {
  if (s == (NSInteger)[sections count]) return @"Changes are written to /var/mobile/fidelio/settings.conf. audiod applies them at once; Spotify and AirPlay need a restart.";
  return nil;
}

- (NSDictionary*)optAt:(NSIndexPath*)ip { return [[[sections objectAtIndex:ip.section] objectForKey:@"options"] objectAtIndex:ip.row]; }

- (UITableViewCell*)tableView:(UITableView*)t cellForRowAtIndexPath:(NSIndexPath*)ip {
  // the trailing section: Advanced toggle + "<daemon> not running" rows
  if (ip.section == (NSInteger)[sections count]) {
    UITableViewCell* c = [[[UITableViewCell alloc] initWithStyle:UITableViewCellStyleSubtitle reuseIdentifier:nil] autorelease];
    if (ip.row == 0) {
      c.textLabel.text = @"Show advanced";
      c.selectionStyle = UITableViewCellSelectionStyleNone;
      UISwitch* sw = [[[UISwitch alloc] init] autorelease];
      sw.on = showAdvanced;
      [sw addTarget:self action:@selector(advancedChanged:) forControlEvents:UIControlEventValueChanged];
      c.accessoryView = sw;
    } else if (ip.row == 1) {
      c.textLabel.text = @"Log remote events";  // B1.16 probe -> /var/mobile/fidelio/remote-events.log
      c.textLabel.font = [UIFont systemFontOfSize:15];
      c.selectionStyle = UITableViewCellSelectionStyleNone;
      UISwitch* sw = [[[UISwitch alloc] init] autorelease];
      sw.on = NPEventProbeEnabled();
      [sw addTarget:self action:@selector(probeChanged:) forControlEvents:UIControlEventValueChanged];
      c.accessoryView = sw;
    } else {
      NSString* d = [missing objectAtIndex:ip.row - 2];
      c.textLabel.text = [NSString stringWithFormat:@"%@ is not running", d];
      c.textLabel.textColor = [UIColor grayColor];
      c.selectionStyle = UITableViewCellSelectionStyleNone;
    }
    return c;
  }
  // B1.16 B: every setting is ONE standard-height row (two lines for a slider): title on the left, the
  // control or value on the right, no description text. A small info (?) button on the row shows the
  // description + default on demand. The lock glyph stays for a command-line-locked option.
  NSDictionary* o = [self optAt:ip];
  NSString* type = [o objectForKey:@"type"];
  BOOL locked = [[o objectForKey:@"locked"] boolValue];
  NSString* val = [o objectForKey:@"value"];
  NSInteger tag = ip.section * 1000 + ip.row;
  BOOL slider = ([type isEqualToString:@"int"] || [type isEqualToString:@"percent"]) && !locked;

  if (slider) {
    // Two-line cell: title + value on line 1 (with the ? button), slider across line 2.
    UITableViewCell* c = [[[UITableViewCell alloc] initWithStyle:UITableViewCellStyleDefault reuseIdentifier:nil] autorelease];
    c.selectionStyle = UITableViewCellSelectionStyleNone;
    CGFloat w = t.bounds.size.width;
    long v = [val longLongValue], lo = [[o objectForKey:@"min"] longLongValue], hi = [[o objectForKey:@"max"] longLongValue];
    NSString* unit = [o objectForKey:@"unit"] ? [o objectForKey:@"unit"] : ([type isEqualToString:@"percent"] ? @"%" : @"");
    UILabel* title = [[[UILabel alloc] initWithFrame:CGRectMake(14, 6, w - 160, 22)] autorelease];
    title.text = [o objectForKey:@"title"]; title.font = [UIFont systemFontOfSize:16]; title.backgroundColor = [UIColor clearColor];
    title.autoresizingMask = UIViewAutoresizingFlexibleWidth;
    [c.contentView addSubview:title];
    UILabel* vlab = [[[UILabel alloc] initWithFrame:CGRectMake(w - 118, 6, 70, 22)] autorelease];
    vlab.text = [NSString stringWithFormat:@"%ld%@", v, unit]; vlab.font = [UIFont systemFontOfSize:16]; vlab.textColor = [UIColor grayColor];
    vlab.textAlignment = UITextAlignmentRight; vlab.backgroundColor = [UIColor clearColor]; vlab.tag = 7001;
    vlab.autoresizingMask = UIViewAutoresizingFlexibleLeftMargin;
    [c.contentView addSubview:vlab];
    UIButton* info = [self infoButton:tag];
    info.frame = CGRectMake(w - 40, 10, 24, 24); info.autoresizingMask = UIViewAutoresizingFlexibleLeftMargin;
    [c.contentView addSubview:info];
    UISlider* sl = [[[UISlider alloc] initWithFrame:CGRectMake(14, 34, w - 28, 24)] autorelease];
    sl.minimumValue = lo; sl.maximumValue = hi; sl.value = v; sl.tag = tag; sl.continuous = NO;
    sl.autoresizingMask = UIViewAutoresizingFlexibleWidth;
    [sl addTarget:self action:@selector(sliderChanged:) forControlEvents:UIControlEventValueChanged];
    [c.contentView addSubview:sl];
    return c;
  }

  // One-line cell (Value1: title left, value right), with the control + ? button as the accessory.
  UITableViewCell* c = [[[UITableViewCell alloc] initWithStyle:UITableViewCellStyleValue1 reuseIdentifier:nil] autorelease];
  c.textLabel.text = [o objectForKey:@"title"];
  c.detailTextLabel.textColor = [UIColor grayColor];
  c.selectionStyle = ([type isEqualToString:@"enum"] || [type isEqualToString:@"string"]) && !locked ? UITableViewCellSelectionStyleBlue : UITableViewCellSelectionStyleNone;

  UIView* acc = [[[UIView alloc] initWithFrame:CGRectMake(0, 0, 0, 30)] autorelease];
  UIButton* info = [self infoButton:tag];
  CGFloat x = 0;
  info.frame = CGRectMake(x, 3, 24, 24); [acc addSubview:info]; x += 30;

  if ([type isEqualToString:@"bool"]) {
    UISwitch* sw = [[[UISwitch alloc] init] autorelease];
    sw.on = [val isEqualToString:@"on"] || [val isEqualToString:@"true"] || [val isEqualToString:@"1"];
    sw.enabled = !locked; sw.tag = tag;
    [sw addTarget:self action:@selector(boolChanged:) forControlEvents:UIControlEventValueChanged];
    CGRect f = sw.frame; f.origin.x = x; f.origin.y = (30 - f.size.height) / 2; sw.frame = f;
    [acc addSubview:sw]; x += f.size.width;
  } else if ([type isEqualToString:@"enum"] || [type isEqualToString:@"string"]) {
    NSString* label = val;
    if ([type isEqualToString:@"enum"]) for (NSDictionary* ch in [o objectForKey:@"choices"]) if ([[ch objectForKey:@"value"] isEqualToString:val]) label = [ch objectForKey:@"label"];
    c.detailTextLabel.text = label;  // Value1 shows this on the right, left of the accessory
    if (locked) { UIView* g = [self lockGlyph]; g.frame = CGRectMake(x, 3, 24, 24); [acc addSubview:g]; x += 24; }
    else { UILabel* ch = [[[UILabel alloc] initWithFrame:CGRectMake(x, 0, 14, 30)] autorelease]; ch.text = @"\u203A"; ch.font = [UIFont systemFontOfSize:22]; ch.textColor = [UIColor colorWithWhite:0.6f alpha:1]; ch.backgroundColor = [UIColor clearColor]; [acc addSubview:ch]; x += 16; }
  }
  acc.frame = CGRectMake(0, 0, x, 30);
  c.accessoryView = acc;
  return c;
}

// A standard info (?) button bound to an option by tag (section*1000 + row).
- (UIButton*)infoButton:(NSInteger)tag {
  UIButton* b = [UIButton buttonWithType:UIButtonTypeInfoLight];
  b.tag = tag;
  [b addTarget:self action:@selector(infoTapped:) forControlEvents:UIControlEventTouchUpInside];
  return b;
}

- (void)infoTapped:(UIButton*)b {
  NSIndexPath* ip = [NSIndexPath indexPathForRow:b.tag % 1000 inSection:b.tag / 1000];
  if (ip.section >= (NSInteger)[sections count]) return;
  NSDictionary* o = [self optAt:ip];
  NSMutableString* m = [NSMutableString stringWithString:[o objectForKey:@"desc"] ? [o objectForKey:@"desc"] : @""];
  NSString* def = [o objectForKey:@"default"];
  if ([def length]) { if ([m length]) [m appendString:@"\n\n"]; [m appendFormat:@"Default: %@", def]; }
  if ([[o objectForKey:@"locked"] boolValue]) [m appendString:@"\n\nSet on the command line; shown here but not editable."];
  else if (![[o objectForKey:@"live"] boolValue]) [m appendString:@"\n\nChanging this restarts the daemon."];
  UIAlertView* a = [[[UIAlertView alloc] initWithTitle:[o objectForKey:@"title"] message:m delegate:nil cancelButtonTitle:@"OK" otherButtonTitles:nil] autorelease];
  [a show];
}

// Row heights: sliders are two-line (66), everything else standard (44).
- (CGFloat)tableView:(UITableView*)t heightForRowAtIndexPath:(NSIndexPath*)ip {
  if (ip.section >= (NSInteger)[sections count]) return 44;
  NSDictionary* o = [self optAt:ip];
  NSString* ty = [o objectForKey:@"type"];
  BOOL slider = ([ty isEqualToString:@"int"] || [ty isEqualToString:@"percent"]) && ![[o objectForKey:@"locked"] boolValue];
  return slider ? 66 : 44;
}

- (UIView*)lockGlyph {
  UILabel* l = [[[UILabel alloc] initWithFrame:CGRectMake(0, 0, 24, 24)] autorelease];
  l.text = @"\U0001F512";  // a padlock; falls back to a box on 3.1 but conveys "locked"
  l.font = [UIFont systemFontOfSize:16];
  return l;
}

- (void)tableView:(UITableView*)t didSelectRowAtIndexPath:(NSIndexPath*)ip {
  [t deselectRowAtIndexPath:ip animated:YES];
  if (ip.section >= (NSInteger)[sections count]) return;
  NSDictionary* o = [self optAt:ip];
  if ([[o objectForKey:@"locked"] boolValue]) return;
  NSString* ty = [o objectForKey:@"type"];
  if ([ty isEqualToString:@"enum"]) [self pushEnumPickerFor:o];
  else if ([ty isEqualToString:@"string"]) [self pushStringEditorFor:o];
}

- (void)advancedChanged:(UISwitch*)sw { showAdvanced = sw.on; [self reload]; }
- (void)probeChanged:(UISwitch*)sw { NPEventProbeSetEnabled(sw.on); }

- (void)boolChanged:(UISwitch*)sw {
  NSIndexPath* ip = [NSIndexPath indexPathForRow:sw.tag % 1000 inSection:sw.tag / 1000];
  NSDictionary* o = [self optAt:ip];
  [self setOption:o value:(sw.on ? @"on" : @"off")];
}

- (void)sliderChanged:(UISlider*)sl {
  NSIndexPath* ip = [NSIndexPath indexPathForRow:sl.tag % 1000 inSection:sl.tag / 1000];
  NSDictionary* o = [self optAt:ip];
  long v = lroundf(sl.value);
  NSString* unit = [o objectForKey:@"unit"] ? [o objectForKey:@"unit"] : ([[o objectForKey:@"type"] isEqualToString:@"percent"] ? @"%" : @"");
  UITableViewCell* cell = [table cellForRowAtIndexPath:ip];
  UILabel* vlab = (UILabel*)[cell.contentView viewWithTag:7001];
  if (vlab) vlab.text = [NSString stringWithFormat:@"%ld%@", v, unit];
  [self setOption:o value:[NSString stringWithFormat:@"%ld", v]];
}

// --- settings.conf write (atomic; preserve lines we do not understand) -----------------------------
- (void)setOption:(NSDictionary*)o value:(NSString*)value {
  NSString* fullKey = [NSString stringWithFormat:@"%@.%@", [o objectForKey:@"daemon"], [o objectForKey:@"key"]];
  NSString* conf = ConfPath();
  NSString* existing = [NSString stringWithContentsOfFile:conf encoding:NSUTF8StringEncoding error:NULL];
  NSMutableArray* lines = existing ? [[[existing componentsSeparatedByString:@"\n"] mutableCopy] autorelease] : [NSMutableArray array];
  BOOL replaced = NO;
  for (NSUInteger i = 0; i < [lines count]; i++) {
    NSString* ln = [[lines objectAtIndex:i] stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceCharacterSet]];
    if ([ln hasPrefix:@"#"] || [ln rangeOfString:@"="].location == NSNotFound) continue;
    NSString* k = [[[ln componentsSeparatedByString:@"="] objectAtIndex:0] stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceCharacterSet]];
    if ([k isEqualToString:fullKey]) { [lines replaceObjectAtIndex:i withObject:[NSString stringWithFormat:@"%@=%@", fullKey, value]]; replaced = YES; break; }
  }
  if (!replaced) [lines addObject:[NSString stringWithFormat:@"%@=%@", fullKey, value]];
  NSString* out = [lines componentsJoinedByString:@"\n"];
  if (![out hasSuffix:@"\n"]) out = [out stringByAppendingString:@"\n"];
  // Write to a temp file then rename over the target; if the target exists, remove it first
  // (moveItemAtPath: fails if the destination is present on this iOS).
  NSString* tmp = [conf stringByAppendingString:@".tmp"];
  NSFileManager* fm = [NSFileManager defaultManager];
  if ([out writeToFile:tmp atomically:NO encoding:NSUTF8StringEncoding error:NULL]) {
    if (![fm moveItemAtPath:tmp toPath:conf error:NULL]) {
      [fm removeItemAtPath:conf error:NULL];
      [fm moveItemAtPath:tmp toPath:conf error:NULL];
    }
  }
  NSLog(@"settings: %@=%@", fullKey, value);
  if (![[o objectForKey:@"live"] boolValue]) [self offerRestart:[o objectForKey:@"daemon"]];
}

- (void)offerRestart:(NSString*)daemon {
  NSString* target = [daemon isEqualToString:@"cspot"] ? @"cspot-ios3" : [daemon isEqualToString:@"raop"] ? @"fidelio-raop" : @"fidelio-audiod";
  UIAlertView* a = [[[UIAlertView alloc] initWithTitle:@"Restart needed"
      message:[NSString stringWithFormat:@"This change needs %@ to restart. Playback will be interrupted briefly.", daemon]
      delegate:self cancelButtonTitle:@"Later" otherButtonTitles:@"Restart", nil] autorelease];
  [pendingRestartTarget release];
  pendingRestartTarget = [target retain];
  [a show];
}

- (void)alertView:(UIAlertView*)av clickedButtonAtIndex:(NSInteger)idx {
  if (idx == av.cancelButtonIndex) return;
  NSString* target = [[pendingRestartTarget retain] autorelease];
  if (!target) return;
  char* argv[] = { (char*)"killall", (char*)"-9", (char*)[target UTF8String], NULL };
  pid_t pid = 0; int rc = posix_spawn(&pid, "/usr/bin/killall", NULL, NULL, argv, environ);
  if (rc == 0) { int ws; waitpid(pid, &ws, 0); }
  NSLog(@"settings restart: killall -9 %@ -> %d", target, rc);
}

- (void)pushEnumPickerFor:(NSDictionary*)o {
  EnumPickerViewController* vc = [[[EnumPickerViewController alloc] initWithOption:o settings:self] autorelease];
  [self.navigationController pushViewController:vc animated:YES];
}

- (void)pushStringEditorFor:(NSDictionary*)o {
  StringEditorViewController* vc = [[[StringEditorViewController alloc] initWithOption:o settings:self] autorelease];
  [self.navigationController pushViewController:vc animated:YES];
}

- (void)dealloc {
  [table release];
  [sections release];
  [missing release];
  [pendingRestartTarget release];
  [super dealloc];
}
@end
