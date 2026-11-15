// B1.15d: the pushed editor a free-text (string) option opens — the device name. A single UITextField
// with the manifest's maxlen enforced and Bonjour-unfriendly characters (control chars) filtered;
// Save writes the value back through the owning SettingsViewController (which offers a restart).
#import <UIKit/UIKit.h>

@class SettingsViewController;

@interface StringEditorViewController : UIViewController <UITextFieldDelegate> {
  NSDictionary* option;             // the manifest option dict (key, title, value, maxlen, daemon, ...)
  NSInteger maxlen;
  UITextField* field;
  SettingsViewController* owner;    // not retained (parent owns us)
}
- (id)initWithOption:(NSDictionary*)o settings:(SettingsViewController*)s;
@end
