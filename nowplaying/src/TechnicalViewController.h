#import <UIKit/UIKit.h>
#import "FidelioStatus.h"

@interface TechnicalViewController : UITableViewController {
  FidelioStatus* status;  // not retained
  NSArray* rows;          // array of 2-element arrays (label, value)
}
- (id)initWithStatus:(FidelioStatus*)s;
- (void)update;
@end
