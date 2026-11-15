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
  UIButton* prevBtn;
  UIButton* playBtn;
  UIButton* nextBtn;
  NSString* notice;         // watchdog note shown instead of the state line until noticeUntil
  NSTimeInterval noticeUntil;
  CGFloat glyphScale;       // B1.18: scale the transport glyphs are drawn at (1 phone, 2 tablet)
  CGFloat titleFontSize;    // B1.18: fonts last applied by layoutContent (0 = none yet)
  int playGlyph;            // 1 = play, 2 = pause: what the play button shows
}
- (void)showNotice:(NSString*)text forSeconds:(NSTimeInterval)seconds;
- (id)initWithStatus:(FidelioStatus*)s;
- (void)update;
@end
