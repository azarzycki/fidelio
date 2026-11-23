#import <UIKit/UIKit.h>
#import "NPStatus.h"
#import "NPKeyLog.h"

#include <sys/types.h>

// B1.18: a plain view controller owning its table (was a UITableViewController), so the tablet can show
// the table in a centred readable column ([NPStatus columnViewForTable:]).
// B1.16.13: section 0 is "Dock remote": the last presses the DockRemote tweak wrote to remote-keys.log,
// newest first, polled twice a second while the tab is visible (-startKeyPolling / -stopKeyPolling).
@interface TechnicalViewController : UIViewController <UITableViewDataSource, UITableViewDelegate> {
  NPStatus* status;  // not retained
  NSArray* rows;          // array of 2-element arrays (label, value)
  UITableView* table;
  NSTimer* keyTimer;      // the 0.5 s key-log poll; nil while the tab is not visible
  NPKeyEntry keys[NP_KEYLOG_SHOWN];
  int keyCount;
  BOOL keyFileKnown;      // keyMtime/keySize/keyIno describe the file as last read
  time_t keyMtime;
  off_t keySize;
  ino_t keyIno;
  BOOL keyFresh;          // the newest press is at most a few seconds old: shown highlighted
}
- (id)initWithStatus:(NPStatus*)s;
- (void)update;
- (void)startKeyPolling;  // idempotent; reads the log at once
- (void)stopKeyPolling;
@end
