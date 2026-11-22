/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Andy Zarzycki. Part of this repository's original work; see LICENSE at its root. */
/* <UIKit/UIKit.h> for the sysroot (toolchain/sysroot/frameworks).
 *
 * Why a file of our own: Apple publishes no source for UIKit's headers. This file is written for this
 * repository and declares only the part of UIKit that its sources use: NowPlaying (nowplaying/src)
 * and the lock-screen plugin (nowplaying/lockplugin). It is an allow-list: a class, method, function
 * or constant that is not here was not needed by any source of the build. See ../../../README.md,
 * "Stage 3", for how to add one.
 *
 * Interface facts stated here, all part of how a caller is compiled:
 *   - for every class, method and property declared: the selector (for a property, its getter and
 *     setter names) and the types of result and arguments, which decide how a message is sent
 *     (structures such as CGRect are returned in memory);
 *   - the values of the enumerations used, and which of them are enumeration types (encoded "i")
 *     and which are NSInteger or NSUInteger;
 *   - the delegate and data-source protocols that classes of ours adopt, with their methods in the
 *     order, the optionality and with the types that the protocol descriptions in this repository's
 *     own objects record (the compiler writes the description of an adopted protocol into the object);
 *   - the instance sizes of the three classes that classes of ours subclass (UIApplication 57 bytes,
 *     UIWindow 88, UIViewController 92), taken from the class descriptions in this repository's own
 *     objects. They are declared as opaque reserved storage. UIResponder and UIView are given no
 *     storage of their own here: their sizes are not known from those objects and are counted in the
 *     subclasses' storage. A source that subclasses UIResponder or UIView directly must first
 *     establish their sizes (see the README).
 */
#if !defined(FIDELIO_SYSROOT_UIKIT_H)
#define FIDELIO_SYSROOT_UIKIT_H

#import <Foundation/Foundation.h>
#include <CoreGraphics/CoreGraphics.h>

#if defined(__cplusplus)
#define UIKIT_EXTERN extern "C"
#else
#define UIKIT_EXTERN extern
#endif

@class CALayer, NSSet, UIColor, UIEvent, UIFont, UIImage, UIScrollView, UIView, UIWindow;
@class UIAlertView, UIApplication, UIBarButtonItem, UINavigationController, UINavigationItem;
@class UITabBarItem, UITableView, UITableViewCell, UITextField, UIViewController;
@protocol UITableViewDataSource, UITableViewDelegate, UITextFieldDelegate;

/* ---- enumerations ---------------------------------------------------------------------------------- */

typedef enum {
    UIInterfaceOrientationPortrait = 1
} UIInterfaceOrientation;

typedef enum {
    UIDeviceBatteryStateUnknown = 0,
    UIDeviceBatteryStateUnplugged = 1,   /* on battery */
    UIDeviceBatteryStateCharging = 2,    /* plugged in, below 100 % */
    UIDeviceBatteryStateFull = 3         /* plugged in, at 100 % */
} UIDeviceBatteryState;

typedef enum {
    UIEventTypeTouches = 0
} UIEventType;
typedef enum {
    UIEventSubtypeNone = 0
} UIEventSubtype;

typedef enum {
    UITouchPhaseBegan = 0,
    UITouchPhaseEnded = 3
} UITouchPhase;

enum {
    UIViewAutoresizingFlexibleLeftMargin = 1 << 0,
    UIViewAutoresizingFlexibleWidth = 1 << 1,
    UIViewAutoresizingFlexibleRightMargin = 1 << 2,
    UIViewAutoresizingFlexibleHeight = 1 << 4
};
typedef NSUInteger UIViewAutoresizing;

typedef enum {
    UIViewContentModeScaleAspectFit = 1,
    UIViewContentModeScaleAspectFill = 2
} UIViewContentMode;

typedef enum {
    UITextAlignmentLeft = 0,
    UITextAlignmentCenter = 1,
    UITextAlignmentRight = 2
} UITextAlignment;

typedef enum {
    UILineBreakModeTailTruncation = 4
} UILineBreakMode;

enum {
    UIControlEventTouchUpInside = 1 << 6,
    UIControlEventValueChanged = 1 << 12
};
typedef NSUInteger UIControlEvents;

enum {
    UIControlStateNormal = 0,
    UIControlStateDisabled = 1 << 1
};
typedef NSUInteger UIControlState;

typedef enum {
    UIButtonTypeCustom = 0,
    UIButtonTypeInfoLight = 3
} UIButtonType;

typedef enum {
    UIProgressViewStyleDefault = 0
} UIProgressViewStyle;

typedef enum {
    UITextBorderStyleRoundedRect = 3
} UITextBorderStyle;

typedef enum {
    UITextFieldViewModeWhileEditing = 1
} UITextFieldViewMode;

typedef enum {
    UITextAutocapitalizationTypeWords = 1
} UITextAutocapitalizationType;

typedef enum {
    UITextAutocorrectionTypeNo = 1
} UITextAutocorrectionType;

typedef enum {
    UIReturnKeyDone = 9
} UIReturnKeyType;

typedef enum {
    UITableViewStylePlain = 0,
    UITableViewStyleGrouped = 1
} UITableViewStyle;

typedef enum {
    UITableViewCellStyleDefault = 0,
    UITableViewCellStyleValue1 = 1,
    UITableViewCellStyleSubtitle = 3
} UITableViewCellStyle;

typedef enum {
    UITableViewCellSelectionStyleNone = 0,
    UITableViewCellSelectionStyleBlue = 1
} UITableViewCellSelectionStyle;

typedef enum {
    UITableViewCellAccessoryNone = 0,
    UITableViewCellAccessoryCheckmark = 3
} UITableViewCellAccessoryType;

typedef enum {
    UITableViewCellEditingStyleNone = 0
} UITableViewCellEditingStyle;

typedef enum {
    UIBarButtonSystemItemSave = 3
} UIBarButtonSystemItem;

/* ---- structures and functions ------------------------------------------------------------------------- */

typedef struct UIEdgeInsets {
    CGFloat top, left, bottom, right;
} UIEdgeInsets;

static inline UIEdgeInsets UIEdgeInsetsMake(CGFloat top, CGFloat left, CGFloat bottom, CGFloat right) {
    UIEdgeInsets e;
    e.top = top;
    e.left = left;
    e.bottom = bottom;
    e.right = right;
    return e;
}

/* principalClassName and delegateClassName name classes (nil: UIApplication, no delegate) */
UIKIT_EXTERN int UIApplicationMain(int argc, char *argv[], NSString *principalClassName, NSString *delegateClassName);

/* an off-screen bitmap context of the given size becomes the current context */
UIKIT_EXTERN void UIGraphicsBeginImageContext(CGSize size);
UIKIT_EXTERN CGContextRef UIGraphicsGetCurrentContext(void);
UIKIT_EXTERN UIImage *UIGraphicsGetImageFromCurrentImageContext(void);
UIKIT_EXTERN void UIGraphicsEndImageContext(void);

/* ---- protocols that classes of ours adopt ------------------------------------------------------------- */
/* The compiler writes the complete description of an adopted protocol (and of the protocols it inherits)
   into the object of the adopting class: every method, required or optional, in order, with its types.
   So these protocols are given in full, as this repository's own objects record them. */

@protocol UIApplicationDelegate <NSObject>
@optional
- (void)applicationDidFinishLaunching:(UIApplication *)application;
- (BOOL)application:(UIApplication *)application didFinishLaunchingWithOptions:(NSDictionary *)launchOptions;
- (void)applicationDidBecomeActive:(UIApplication *)application;
- (void)applicationWillResignActive:(UIApplication *)application;
- (BOOL)application:(UIApplication *)application handleOpenURL:(NSURL *)url;
- (void)applicationDidReceiveMemoryWarning:(UIApplication *)application;
- (void)applicationWillTerminate:(UIApplication *)application;
- (void)applicationSignificantTimeChange:(UIApplication *)application;
- (void)application:(UIApplication *)application willChangeStatusBarOrientation:(UIInterfaceOrientation)newOrientation
           duration:(NSTimeInterval)duration;
- (void)application:(UIApplication *)application didChangeStatusBarOrientation:(UIInterfaceOrientation)oldOrientation;
- (void)application:(UIApplication *)application willChangeStatusBarFrame:(CGRect)newFrame;
- (void)application:(UIApplication *)application didChangeStatusBarFrame:(CGRect)oldFrame;
- (void)application:(UIApplication *)application didRegisterForRemoteNotificationsWithDeviceToken:(NSData *)token;
- (void)application:(UIApplication *)application didFailToRegisterForRemoteNotificationsWithError:(NSError *)error;
- (void)application:(UIApplication *)application didReceiveRemoteNotification:(NSDictionary *)userInfo;
@end

@protocol UIScrollViewDelegate <NSObject>
@optional
- (void)scrollViewDidScroll:(UIScrollView *)scrollView;
- (void)scrollViewWillBeginDragging:(UIScrollView *)scrollView;
- (void)scrollViewDidEndDragging:(UIScrollView *)scrollView willDecelerate:(BOOL)decelerate;
- (void)scrollViewWillBeginDecelerating:(UIScrollView *)scrollView;
- (void)scrollViewDidEndDecelerating:(UIScrollView *)scrollView;
- (void)scrollViewDidEndScrollingAnimation:(UIScrollView *)scrollView;
- (UIView *)viewForZoomingInScrollView:(UIScrollView *)scrollView;
- (void)scrollViewDidEndZooming:(UIScrollView *)scrollView withView:(UIView *)view atScale:(float)scale;
- (BOOL)scrollViewShouldScrollToTop:(UIScrollView *)scrollView;
- (void)scrollViewDidScrollToTop:(UIScrollView *)scrollView;
@end

@protocol UITableViewDataSource <NSObject>
@required
- (NSInteger)tableView:(UITableView *)tableView numberOfRowsInSection:(NSInteger)section;
- (UITableViewCell *)tableView:(UITableView *)tableView cellForRowAtIndexPath:(NSIndexPath *)indexPath;
@optional
- (NSInteger)numberOfSectionsInTableView:(UITableView *)tableView;
- (NSString *)tableView:(UITableView *)tableView titleForHeaderInSection:(NSInteger)section;
- (NSString *)tableView:(UITableView *)tableView titleForFooterInSection:(NSInteger)section;
- (BOOL)tableView:(UITableView *)tableView canEditRowAtIndexPath:(NSIndexPath *)indexPath;
- (BOOL)tableView:(UITableView *)tableView canMoveRowAtIndexPath:(NSIndexPath *)indexPath;
- (NSArray *)sectionIndexTitlesForTableView:(UITableView *)tableView;
- (NSInteger)tableView:(UITableView *)tableView sectionForSectionIndexTitle:(NSString *)title atIndex:(NSInteger)index;
- (void)tableView:(UITableView *)tableView commitEditingStyle:(UITableViewCellEditingStyle)editingStyle
    forRowAtIndexPath:(NSIndexPath *)indexPath;
- (void)tableView:(UITableView *)tableView moveRowAtIndexPath:(NSIndexPath *)fromIndexPath
      toIndexPath:(NSIndexPath *)toIndexPath;
@end

@protocol UITableViewDelegate <NSObject, UIScrollViewDelegate>
@optional
- (void)tableView:(UITableView *)tableView willDisplayCell:(UITableViewCell *)cell forRowAtIndexPath:(NSIndexPath *)indexPath;
- (CGFloat)tableView:(UITableView *)tableView heightForRowAtIndexPath:(NSIndexPath *)indexPath;
- (CGFloat)tableView:(UITableView *)tableView heightForHeaderInSection:(NSInteger)section;
- (CGFloat)tableView:(UITableView *)tableView heightForFooterInSection:(NSInteger)section;
- (UIView *)tableView:(UITableView *)tableView viewForHeaderInSection:(NSInteger)section;
- (UIView *)tableView:(UITableView *)tableView viewForFooterInSection:(NSInteger)section;
- (UITableViewCellAccessoryType)tableView:(UITableView *)tableView accessoryTypeForRowWithIndexPath:(NSIndexPath *)indexPath;
- (void)tableView:(UITableView *)tableView accessoryButtonTappedForRowWithIndexPath:(NSIndexPath *)indexPath;
- (NSIndexPath *)tableView:(UITableView *)tableView willSelectRowAtIndexPath:(NSIndexPath *)indexPath;
- (NSIndexPath *)tableView:(UITableView *)tableView willDeselectRowAtIndexPath:(NSIndexPath *)indexPath;
- (void)tableView:(UITableView *)tableView didSelectRowAtIndexPath:(NSIndexPath *)indexPath;
- (void)tableView:(UITableView *)tableView didDeselectRowAtIndexPath:(NSIndexPath *)indexPath;
- (UITableViewCellEditingStyle)tableView:(UITableView *)tableView editingStyleForRowAtIndexPath:(NSIndexPath *)indexPath;
- (NSString *)tableView:(UITableView *)tableView titleForDeleteConfirmationButtonForRowAtIndexPath:(NSIndexPath *)indexPath;
- (BOOL)tableView:(UITableView *)tableView shouldIndentWhileEditingRowAtIndexPath:(NSIndexPath *)indexPath;
- (void)tableView:(UITableView *)tableView willBeginEditingRowAtIndexPath:(NSIndexPath *)indexPath;
- (void)tableView:(UITableView *)tableView didEndEditingRowAtIndexPath:(NSIndexPath *)indexPath;
- (NSIndexPath *)tableView:(UITableView *)tableView targetIndexPathForMoveFromRowAtIndexPath:(NSIndexPath *)sourceIndexPath
       toProposedIndexPath:(NSIndexPath *)proposedDestinationIndexPath;
- (NSInteger)tableView:(UITableView *)tableView indentationLevelForRowAtIndexPath:(NSIndexPath *)indexPath;
@end

@protocol UIAlertViewDelegate <NSObject>
@optional
- (void)alertView:(UIAlertView *)alertView clickedButtonAtIndex:(NSInteger)buttonIndex;
- (void)alertViewCancel:(UIAlertView *)alertView;
- (void)willPresentAlertView:(UIAlertView *)alertView;
- (void)didPresentAlertView:(UIAlertView *)alertView;
- (void)alertView:(UIAlertView *)alertView willDismissWithButtonIndex:(NSInteger)buttonIndex;
- (void)alertView:(UIAlertView *)alertView didDismissWithButtonIndex:(NSInteger)buttonIndex;
@end

@protocol UITextFieldDelegate <NSObject>
@optional
- (BOOL)textFieldShouldBeginEditing:(UITextField *)textField;
- (void)textFieldDidBeginEditing:(UITextField *)textField;
- (BOOL)textFieldShouldEndEditing:(UITextField *)textField;
- (void)textFieldDidEndEditing:(UITextField *)textField;
- (BOOL)textField:(UITextField *)textField shouldChangeCharactersInRange:(NSRange)range replacementString:(NSString *)string;
- (BOOL)textFieldShouldClear:(UITextField *)textField;
- (BOOL)textFieldShouldReturn:(UITextField *)textField;
@end

/* ---- application, events, device ------------------------------------------------------------------------ */

@interface UIResponder : NSObject
- (BOOL)canBecomeFirstResponder;
- (BOOL)becomeFirstResponder;
- (BOOL)isFirstResponder;
@end

@interface UIApplication : UIResponder {
  @private
    char _fidelioReserved[53];   /* instances are 57 bytes; see the comment at the top */
}
@property(nonatomic, getter=isIdleTimerDisabled) BOOL idleTimerDisabled;
- (void)setStatusBarHidden:(BOOL)hidden animated:(BOOL)animated;
- (void)sendEvent:(UIEvent *)event;
@end

@interface UIEvent : NSObject
@property(nonatomic, readonly) UIEventType type;
@property(nonatomic, readonly) UIEventSubtype subtype;
@property(nonatomic, readonly) NSTimeInterval timestamp;
- (NSSet *)allTouches;
@end

@interface UITouch : NSObject
@property(nonatomic, readonly) UITouchPhase phase;
@end

@interface UIDevice : NSObject
+ (UIDevice *)currentDevice;
@property(nonatomic, getter=isBatteryMonitoringEnabled) BOOL batteryMonitoringEnabled;
@property(nonatomic, readonly) UIDeviceBatteryState batteryState;
@property(nonatomic, readonly) float batteryLevel;   /* 0 .. 1, -1 when unknown */
@end

@interface UIScreen : NSObject
+ (UIScreen *)mainScreen;
@property(nonatomic, readonly) CGRect bounds;
@property(nonatomic, readonly) CGRect applicationFrame;
@end

/* ---- views ---------------------------------------------------------------------------------------------- */

@interface UIView : UIResponder
- (id)initWithFrame:(CGRect)frame;
@property(nonatomic) CGRect frame;
@property(nonatomic) CGRect bounds;
@property(nonatomic) NSInteger tag;
@property(nonatomic, getter=isHidden) BOOL hidden;
@property(nonatomic, getter=isUserInteractionEnabled) BOOL userInteractionEnabled;
@property(nonatomic, copy) UIColor *backgroundColor;
@property(nonatomic) UIViewAutoresizing autoresizingMask;
@property(nonatomic) UIViewContentMode contentMode;
@property(nonatomic, readonly, retain) CALayer *layer;
@property(nonatomic, readonly) UIWindow *window;
@property(nonatomic, readonly, copy) NSArray *subviews;
- (void)addSubview:(UIView *)view;
- (UIView *)viewWithTag:(NSInteger)tag;
@end

@interface UIWindow : UIView {
  @private
    void *_fidelioReserved[21];  /* with UIView's storage: instances are 88 bytes; see the top */
}
- (void)makeKeyAndVisible;
@end

@interface UIColor : NSObject
+ (UIColor *)colorWithWhite:(CGFloat)white alpha:(CGFloat)alpha;
+ (UIColor *)colorWithRed:(CGFloat)red green:(CGFloat)green blue:(CGFloat)blue alpha:(CGFloat)alpha;
+ (UIColor *)blackColor;
+ (UIColor *)whiteColor;
+ (UIColor *)grayColor;
+ (UIColor *)clearColor;
+ (UIColor *)groupTableViewBackgroundColor;
@end

@interface UIFont : NSObject
+ (UIFont *)systemFontOfSize:(CGFloat)fontSize;
+ (UIFont *)boldSystemFontOfSize:(CGFloat)fontSize;
@end

@interface UIImage : NSObject
+ (UIImage *)imageWithContentsOfFile:(NSString *)path;
- (id)initWithContentsOfFile:(NSString *)path;
@property(nonatomic, readonly) CGSize size;
- (void)drawInRect:(CGRect)rect;
@end

@interface UIImageView : UIView
@property(nonatomic, retain) UIImage *image;
@end

@interface UILabel : UIView
@property(nonatomic, copy) NSString *text;
@property(nonatomic, retain) UIFont *font;
@property(nonatomic, retain) UIColor *textColor;
@property(nonatomic) UITextAlignment textAlignment;
@property(nonatomic) UILineBreakMode lineBreakMode;
@property(nonatomic) BOOL adjustsFontSizeToFitWidth;
@property(nonatomic) CGFloat minimumFontSize;
@end

@interface UIProgressView : UIView
- (id)initWithProgressViewStyle:(UIProgressViewStyle)style;
@property(nonatomic) float progress;
@end

@interface UIAlertView : UIView
- (id)initWithTitle:(NSString *)title message:(NSString *)message delegate:(id)delegate
  cancelButtonTitle:(NSString *)cancelButtonTitle otherButtonTitles:(NSString *)otherButtonTitles, ...;
@property(nonatomic) NSInteger cancelButtonIndex;
- (void)show;
@end

/* ---- controls -------------------------------------------------------------------------------------------- */

@interface UIControl : UIView
@property(nonatomic, getter=isEnabled) BOOL enabled;
- (void)addTarget:(id)target action:(SEL)action forControlEvents:(UIControlEvents)controlEvents;
@end

@interface UIButton : UIControl
+ (id)buttonWithType:(UIButtonType)buttonType;
- (void)setImage:(UIImage *)image forState:(UIControlState)state;
- (UIImage *)imageForState:(UIControlState)state;
@end

@interface UISwitch : UIControl
@property(nonatomic, getter=isOn) BOOL on;
@end

@interface UISlider : UIControl
@property(nonatomic) float value;
@property(nonatomic) float minimumValue;
@property(nonatomic) float maximumValue;
@property(nonatomic, getter=isContinuous) BOOL continuous;
@end

@interface UITextField : UIControl
@property(nonatomic, copy) NSString *text;
@property(nonatomic, copy) NSString *placeholder;
@property(nonatomic) UITextBorderStyle borderStyle;
@property(nonatomic) UITextFieldViewMode clearButtonMode;
@property(nonatomic, assign) id<UITextFieldDelegate> delegate;
/* text input traits */
@property(nonatomic) UITextAutocapitalizationType autocapitalizationType;
@property(nonatomic) UITextAutocorrectionType autocorrectionType;
@property(nonatomic) UIReturnKeyType returnKeyType;
@end

/* ---- tables ---------------------------------------------------------------------------------------------- */

@interface UIScrollView : UIView
@property(nonatomic) UIEdgeInsets contentInset;
@property(nonatomic) UIEdgeInsets scrollIndicatorInsets;
@property(nonatomic, getter=isScrollEnabled) BOOL scrollEnabled;
@end

@interface UITableView : UIScrollView
- (id)initWithFrame:(CGRect)frame style:(UITableViewStyle)style;
@property(nonatomic, assign) id<UITableViewDataSource> dataSource;
@property(nonatomic, assign) id<UITableViewDelegate> delegate;
@property(nonatomic) CGFloat rowHeight;
@property(nonatomic, retain) UIColor *separatorColor;
@property(nonatomic) BOOL allowsSelection;
- (void)reloadData;
- (UITableViewCell *)cellForRowAtIndexPath:(NSIndexPath *)indexPath;
- (UITableViewCell *)dequeueReusableCellWithIdentifier:(NSString *)identifier;
- (void)deselectRowAtIndexPath:(NSIndexPath *)indexPath animated:(BOOL)animated;
@end

@interface UITableViewCell : UIView
- (id)initWithStyle:(UITableViewCellStyle)style reuseIdentifier:(NSString *)reuseIdentifier;
@property(nonatomic, readonly, retain) UIView *contentView;
@property(nonatomic, readonly, retain) UILabel *textLabel;
@property(nonatomic, readonly, retain) UILabel *detailTextLabel;
@property(nonatomic) UITableViewCellSelectionStyle selectionStyle;
@property(nonatomic) UITableViewCellAccessoryType accessoryType;
@property(nonatomic, retain) UIView *accessoryView;
@end

/* rows and sections of a table */
@interface NSIndexPath (UITableView)
+ (NSIndexPath *)indexPathForRow:(NSUInteger)row inSection:(NSUInteger)section;
@property(nonatomic, readonly) NSUInteger section;
@property(nonatomic, readonly) NSUInteger row;
@end

/* ---- view controllers and bars ---------------------------------------------------------------------------- */

@interface UIBarItem : NSObject
@end

@interface UITabBarItem : UIBarItem
- (id)initWithTitle:(NSString *)title image:(UIImage *)image tag:(NSInteger)tag;
@end

@interface UIBarButtonItem : UIBarItem
- (id)initWithBarButtonSystemItem:(UIBarButtonSystemItem)systemItem target:(id)target action:(SEL)action;
@end

@interface UINavigationItem : NSObject
@property(nonatomic, retain) UIBarButtonItem *rightBarButtonItem;
@end

@interface UIViewController : UIResponder {
  @private
    void *_fidelioReserved[22];  /* with UIResponder's storage: instances are 92 bytes; see the top */
}
- (id)initWithNibName:(NSString *)nibNameOrNil bundle:(NSBundle *)nibBundleOrNil;
@property(nonatomic, retain) UIView *view;
@property(nonatomic, copy) NSString *title;
@property(nonatomic, retain) UITabBarItem *tabBarItem;
@property(nonatomic, readonly, retain) UINavigationItem *navigationItem;
@property(nonatomic, readonly, retain) UINavigationController *navigationController;
- (void)viewDidLoad;
- (void)viewDidUnload;
- (void)viewWillAppear:(BOOL)animated;
- (void)viewDidAppear:(BOOL)animated;
@end

@interface UINavigationController : UIViewController
- (id)initWithRootViewController:(UIViewController *)rootViewController;
- (void)pushViewController:(UIViewController *)viewController animated:(BOOL)animated;
- (UIViewController *)popViewControllerAnimated:(BOOL)animated;
@end

@interface UITabBarController : UIViewController
@property(nonatomic, copy) NSArray *viewControllers;
@property(nonatomic, assign) UIViewController *selectedViewController;
@end

#endif /* FIDELIO_SYSROOT_UIKIT_H */
