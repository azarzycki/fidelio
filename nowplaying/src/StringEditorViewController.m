#import "StringEditorViewController.h"
#import "SettingsViewController.h"
#import "NPStatus.h"  // B1.18: isTablet, allowsOrientation, columnViewForTable

@implementation StringEditorViewController

- (id)initWithOption:(NSDictionary*)o settings:(SettingsViewController*)s {
  if ((self = [super init])) {
    option = [o retain];
    owner = s;
    maxlen = [o objectForKey:@"maxlen"] ? [[o objectForKey:@"maxlen"] integerValue] : 63;
    if (maxlen <= 0) maxlen = 63;
    self.title = [o objectForKey:@"title"];
  }
  return self;
}

- (void)loadView {
  UIView* v = [[[UIView alloc] initWithFrame:[UIScreen mainScreen].applicationFrame] autorelease];
  v.backgroundColor = [UIColor groupTableViewBackgroundColor];
  v.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
  CGFloat w = v.bounds.size.width;
  // B1.18: on the tablet the field is a centred 600 pt column instead of the full width
  CGFloat fw = [NPStatus isTablet] && w > 640 ? 600 : w - 20;
  field = [[UITextField alloc] initWithFrame:CGRectMake((w - fw) / 2, 20, fw, 34)];
  field.borderStyle = UITextBorderStyleRoundedRect;
  field.text = [option objectForKey:@"value"];
  field.placeholder = [option objectForKey:@"title"];
  field.autocorrectionType = UITextAutocorrectionTypeNo;
  field.autocapitalizationType = UITextAutocapitalizationTypeWords;
  field.clearButtonMode = UITextFieldViewModeWhileEditing;
  field.returnKeyType = UIReturnKeyDone;
  field.delegate = self;
  field.autoresizingMask = [NPStatus isTablet] ? (UIViewAutoresizingFlexibleLeftMargin | UIViewAutoresizingFlexibleRightMargin)
                                                    : UIViewAutoresizingFlexibleWidth;
  [v addSubview:field];
  self.view = v;
  self.navigationItem.rightBarButtonItem =
      [[[UIBarButtonItem alloc] initWithBarButtonSystemItem:UIBarButtonSystemItemSave target:self action:@selector(save)] autorelease];
}

- (void)viewDidAppear:(BOOL)animated { [super viewDidAppear:animated]; [field becomeFirstResponder]; }

- (BOOL)shouldAutorotateToInterfaceOrientation:(UIInterfaceOrientation)o { return [NPStatus allowsOrientation:o]; }  // B1.18

// Enforce maxlen (bytes) and reject control characters as they are typed.
- (BOOL)textField:(UITextField*)tf shouldChangeCharactersInRange:(NSRange)range replacementString:(NSString*)s {
  for (NSUInteger i = 0; i < [s length]; i++) { unichar c = [s characterAtIndex:i]; if (c < 0x20 || c == 0x7f) return NO; }
  NSString* next = [tf.text stringByReplacingCharactersInRange:range withString:s];
  return (NSInteger)[next lengthOfBytesUsingEncoding:NSUTF8StringEncoding] <= maxlen;
}

- (BOOL)textFieldShouldReturn:(UITextField*)tf { [self save]; return NO; }

- (void)save {
  NSString* v = [field.text stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceCharacterSet]];
  if ([v length] > 0) [owner setOption:option value:v];  // empty is rejected by the daemon anyway
  [self.navigationController popViewControllerAnimated:YES];
}

- (void)dealloc {
  [field release];
  [option release];
  [super dealloc];
}
@end
