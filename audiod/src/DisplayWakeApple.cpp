// B1.9: wake the display and foreground the NowPlaying app via SpringBoardServices (private),
// iPhone OS 3.1. Symbols confirmed exported by the SDK's SpringBoardServices binary (nm):
//   SBSSpringBoardServerPort            mach_port_t (void)
//   SBSLaunchApplicationWithIdentifier  int (CFStringRef displayIdentifier, Boolean suspended)
//   SBDimScreen                         kern_return_t (mach_port_t serverPort, Boolean dim)   [MIG stub, 1 byte arg]
// and by GraphicsServices:
//   GSEventResetIdleTimer               void (void)
// There is no SBSUndimScreen in 3.1; SBDimScreen(port, false) is the undim call of that era (symbol
// confirmed, undim semantics assumed; verified on the device). What a LOCKED device does with the
// launch request cannot be decided off-device: expected is that the screen wakes (undim + idle
// reset) and the launch is refused or deferred behind the lock; unlock is not attempted.
#include <dlfcn.h>
#include <mach/mach.h>
#include <stdio.h>

#include <CoreFoundation/CoreFoundation.h>

#include "DisplayWake.h"
#include "ios3_trace.h"

namespace {

typedef mach_port_t (*ServerPortFn)(void);
typedef int (*LaunchFn)(CFStringRef, Boolean);
typedef kern_return_t (*DimFn)(mach_port_t, Boolean);
typedef void (*ResetIdleFn)(void);

class SpringBoardWaker : public DisplayWaker {
 public:
  SpringBoardWaker() {
    void* sbs = dlopen("/System/Library/PrivateFrameworks/SpringBoardServices.framework/SpringBoardServices", RTLD_LAZY);
    void* gs = dlopen("/System/Library/PrivateFrameworks/GraphicsServices.framework/GraphicsServices", RTLD_LAZY);
    if (sbs) {
      serverPort = (ServerPortFn)dlsym(sbs, "SBSSpringBoardServerPort");
      launch = (LaunchFn)dlsym(sbs, "SBSLaunchApplicationWithIdentifier");
      dim = (DimFn)dlsym(sbs, "SBDimScreen");
    }
    if (gs) resetIdle = (ResetIdleFn)dlsym(gs, "GSEventResetIdleTimer");
    if (!sbs) why = "dlopen SpringBoardServices failed";
    else if (!launch) why = "SBSLaunchApplicationWithIdentifier missing";
  }
  bool wake(const std::string& bundleId) override {
    if (!launch) {
      if (!unavailableLogged) { IOS3_TRACE("wake-on-play: unavailable (%s): no-op", why.c_str()); unavailableLogged = true; }
      return false;
    }
    // 1. undim / wake the screen (best effort), 2. reset the idle timer, 3. foreground the app
    kern_return_t kr = KERN_SUCCESS;
    if (dim && serverPort) { mach_port_t p = serverPort(); if (p) kr = dim(p, 0); }
    if (resetIdle) resetIdle();
    CFStringRef id = CFStringCreateWithCString(nullptr, bundleId.c_str(), kCFStringEncodingUTF8);
    int rc = launch(id, 0);
    CFRelease(id);
    IOS3_TRACE("wake-on-play: SBDimScreen(false) -> %d, GSEventResetIdleTimer %s, SBSLaunchApplicationWithIdentifier(%s) -> %d%s", (int)kr,
               resetIdle ? "called" : "missing", bundleId.c_str(), rc, rc == 0 ? "" : " (refused: locked device or unknown app?)");
    return rc == 0;
  }
  std::string describe() override {
    return launch ? std::string("SpringBoardServices (undim: ") + (dim && serverPort ? "SBDimScreen" : "missing") + ", idle reset: " + (resetIdle ? "yes" : "no") + ")"
                  : "SpringBoardServices unavailable: " + why;
  }

 private:
  ServerPortFn serverPort = nullptr;
  LaunchFn launch = nullptr;
  DimFn dim = nullptr;
  ResetIdleFn resetIdle = nullptr;
  std::string why;
  bool unavailableLogged = false;
};

}  // namespace

DisplayWaker* createSpringBoardWaker() { return new SpringBoardWaker(); }
