// B1.14: a Settings tab built entirely from the daemons' manifests (settings-<daemon>.manifest.plist).
// No option is hard-coded here: one section per group, a control per option type, locked options shown
// but disabled, an "Advanced" switch that hides advanced options. Changes are written to the shared
// /var/mobile/fidelio/settings.conf (atomic, unknown lines preserved); audiod applies live, cspot/raop
// need a restart (offered via killall + launchd KeepAlive).
#import <UIKit/UIKit.h>

@interface SettingsViewController : UIViewController <UITableViewDataSource, UITableViewDelegate, UIAlertViewDelegate> {
  UITableView* table;
  NSMutableArray* sections;   // array of @{ @"title":group, @"options":[opt,...] }
  NSMutableArray* missing;    // daemon names with no manifest
  BOOL showAdvanced;
  NSDate* loadedMtime;        // of the settings dir, to reload on change
  NSString* pendingRestartTarget;  // process name to killall when a restart alert is confirmed
}
- (void)update;
// Called by EnumPickerViewController when the user picks a choice.
- (void)setOption:(NSDictionary*)o value:(NSString*)value;
@end
