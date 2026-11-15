// B1.14: the pushed list that an enum option opens. Shows one row per choice with a checkmark on the
// current value; tapping a row writes it back through the owning SettingsViewController and pops.
#import <UIKit/UIKit.h>

@class SettingsViewController;

@interface EnumPickerViewController : UIViewController <UITableViewDataSource, UITableViewDelegate> {
  UITableView* table;
  NSDictionary* option;             // the manifest option dict (key, title, choices, value, daemon, ...)
  NSArray* choices;                 // array of @{ @"value":..., @"label":... }
  NSString* current;                // currently selected value
  SettingsViewController* owner;    // not retained (parent owns us)
}
- (id)initWithOption:(NSDictionary*)o settings:(SettingsViewController*)s;
@end
