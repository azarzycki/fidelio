// B1.16 probe: a UIApplication subclass that, while the probe is on, logs every event that reaches
// the frontmost app to /var/mobile/dockreceiver/remote-events.log. This is a diagnostic for the dock
// remote: the device capture showed iapd sends Simple Remote keys to NAMED apps (the iPod app and
// SpringBoard), not the frontmost app, so on iPhone OS 3.1 pressing a remote key is expected to log
// NOTHING here (confirming the keys never reach a third-party app). Touch events are logged sparsely
// (began/ended only) so a key press, if it did arrive, would stand out. The probe is off unless
// /var/mobile/dockreceiver/remote-probe.on exists (toggle it from the Technical tab or by touch'ing the
// file over SSH); checking it is throttled so it never costs anything per event.
#import <UIKit/UIKit.h>

@interface NPApplication : UIApplication
@end

// Toggle helpers (used by the Technical tab's "Log remote events" row).
BOOL NPEventProbeEnabled(void);
void NPEventProbeSetEnabled(BOOL on);
