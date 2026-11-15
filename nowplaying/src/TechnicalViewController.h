#import <UIKit/UIKit.h>
#import "FidelioStatus.h"

// B1.18: a plain view controller owning its table (was a UITableViewController), so the tablet can show
// the table in a centred readable column ([FidelioStatus columnViewForTable:]).
@interface TechnicalViewController : UIViewController <UITableViewDataSource, UITableViewDelegate> {
  FidelioStatus* status;  // not retained
  NSArray* rows;          // array of 2-element arrays (label, value)
  UITableView* table;
}
- (id)initWithStatus:(FidelioStatus*)s;
- (void)update;
@end
