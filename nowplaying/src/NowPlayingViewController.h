#import <UIKit/UIKit.h>
#import "FidelioStatus.h"

@interface NowPlayingViewController : UIViewController {
  FidelioStatus* status;  // not retained (owned by the app delegate)
  UIImageView* coverView;
  UIImage* placeholder;
  UILabel* titleLabel;
  UILabel* artistLabel;
  UILabel* albumLabel;
  UIProgressView* progress;
  UILabel* elapsedLabel;
  UILabel* remainingLabel;
  UILabel* stateLabel;
  NSString* notice;         // watchdog note shown instead of the state line until noticeUntil
  NSTimeInterval noticeUntil;
}
- (void)showNotice:(NSString*)text forSeconds:(NSTimeInterval)seconds;
- (id)initWithStatus:(FidelioStatus*)s;
- (void)update;
@end
