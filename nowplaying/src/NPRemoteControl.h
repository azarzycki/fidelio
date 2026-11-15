// B1.16.9: the iPad's dock remote, through NowPlaying acting as a media controller. iOS 4+ only;
// everything here is a no-op on iPhone OS 3.1 (the phone's dock remote is the DockRemote tweak).
//
// On the 5.1.1 iPad, iapd decodes the dock's Simple Remote keys and sends them with
// MRMediaRemoteSendCommand to the NOW-PLAYING app, where UIKit delivers them as UIEventTypeRemoteControl
// events. The open question is what NowPlaying needs in order to receive them while the audio comes
// from the daemons (cspot/raop -> audiod), not from the app. So this is a STAGED PROTOTYPE. The stage
// is read at launch from /var/mobile/fidelio/remote-control.stage (absent = 1); restart the app after
// changing it.
//   1  beginReceivingRemoteControlEvents, with the window kept as first responder. Every remote-control
//      event is logged to /var/mobile/fidelio/remote-control.log. Play, pause, stop, toggle, next and
//      previous are forwarded to audiod (CONTROL), like the on-screen buttons.
//   2  stage 1 + MRMediaRemoteSetCanBeNowPlayingApplication(true) (MediaRemote via dlsym, present on 5.1.1).
//   3  stage 2 + silence played in a MIXABLE audio session. Mixable on purpose: audiod's own session is a
//      non-mixable MediaPlayback session, which a non-mixable one here would interrupt.
// Below stage 3 nothing here touches audio, and stage 3 cannot interrupt audiod.
#import <UIKit/UIKit.h>

// The window class: it can be first responder, so remote-control events always have a responder.
@interface NPWindow : UIWindow
@end

// At launch (after makeKeyAndVisible): reads the stage, turns remote-control events on (iOS 4+), logs it.
void NPRemoteControlSetup(UIApplication* app);
// Make the view's window first responder again (from each tab's viewDidAppear and on activation). iOS 4+ only.
void NPRemoteControlClaimFirstResponder(UIView* anyViewInWindow);
// Handle one event if it is a remote-control event: log it and forward the transport verb to audiod.
// Returns NO for any other event. The same event arriving by two paths (sendEvent: and the responder
// chain) is handled once.
BOOL NPRemoteControlHandle(UIEvent* event, NSString* via);
// Send one transport verb (AUDIOD_CTRL_*, audiod/proto.h) to audiod. Also used by the on-screen buttons.
void NPSendControl(uint8_t verb);
