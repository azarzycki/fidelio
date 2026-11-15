#import "EnumPickerViewController.h"
#import "SettingsViewController.h"
#import "FidelioStatus.h"  // B1.18: isTablet, allowsOrientation, columnViewForTable

@implementation EnumPickerViewController

- (id)initWithOption:(NSDictionary*)o settings:(SettingsViewController*)s {
  if ((self = [super init])) {
    option = [o retain];
    choices = [[o objectForKey:@"choices"] retain];
    current = [[o objectForKey:@"value"] retain];
    owner = s;  // parent owns us while pushed; do not retain (avoid a cycle)
    self.title = [o objectForKey:@"title"];
  }
  return self;
}

- (void)loadView {
  table = [[UITableView alloc] initWithFrame:[UIScreen mainScreen].applicationFrame style:UITableViewStyleGrouped];
  table.dataSource = self;
  table.delegate = self;
  table.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
  self.view = [FidelioStatus columnViewForTable:table background:[UIColor groupTableViewBackgroundColor]];  // B1.18
}

- (BOOL)shouldAutorotateToInterfaceOrientation:(UIInterfaceOrientation)o { return [FidelioStatus allowsOrientation:o]; }  // B1.18

- (NSInteger)tableView:(UITableView*)t numberOfRowsInSection:(NSInteger)s { return (NSInteger)[choices count]; }

- (NSString*)tableView:(UITableView*)t titleForFooterInSection:(NSInteger)s { return [option objectForKey:@"desc"]; }

- (UITableViewCell*)tableView:(UITableView*)t cellForRowAtIndexPath:(NSIndexPath*)ip {
  UITableViewCell* c = [t dequeueReusableCellWithIdentifier:@"c"];
  if (!c) c = [[[UITableViewCell alloc] initWithStyle:UITableViewCellStyleDefault reuseIdentifier:@"c"] autorelease];
  NSDictionary* ch = [choices objectAtIndex:ip.row];
  NSString* label = [ch objectForKey:@"label"]; if (![label isKindOfClass:[NSString class]]) label = [ch objectForKey:@"value"];
  c.textLabel.text = label;
  c.accessoryType = [[ch objectForKey:@"value"] isEqualToString:current] ? UITableViewCellAccessoryCheckmark : UITableViewCellAccessoryNone;
  return c;
}

- (void)tableView:(UITableView*)t didSelectRowAtIndexPath:(NSIndexPath*)ip {
  [t deselectRowAtIndexPath:ip animated:YES];
  NSString* v = [[choices objectAtIndex:ip.row] objectForKey:@"value"];
  [current release]; current = [v retain];
  [t reloadData];
  [owner setOption:option value:v];
  [self.navigationController popViewControllerAnimated:YES];
}

- (void)dealloc {
  [table release];
  [option release];
  [choices release];
  [current release];
  [super dealloc];
}
@end
