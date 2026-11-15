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
#include "ObjcPool.h"
#include "ios3_trace.h"

namespace {

typedef mach_port_t (*ServerPortFn)(void);
typedef int (*LaunchFn)(CFStringRef, Boolean);
typedef kern_return_t (*DimFn)(mach_port_t, Boolean);
typedef void (*ResetIdleFn)(void);
typedef kern_return_t (*EnableLockFn)(mach_port_t, unsigned char, const char*);  // SBEnableLockScreenBundle (B1.11)
typedef CFStringRef (*LaunchErrStrFn)(int);  // B1.15 A: SBSApplicationLaunchingErrorString(code) -> message

class SpringBoardWaker : public DisplayWaker {
 public:
  SpringBoardWaker() {
    void* sbs = dlopen("/System/Library/PrivateFrameworks/SpringBoardServices.framework/SpringBoardServices", RTLD_LAZY);
    void* gs = dlopen("/System/Library/PrivateFrameworks/GraphicsServices.framework/GraphicsServices", RTLD_LAZY);
    if (sbs) {
      serverPort = (ServerPortFn)dlsym(sbs, "SBSSpringBoardServerPort");
      launch = (LaunchFn)dlsym(sbs, "SBSLaunchApplicationWithIdentifier");
      dim = (DimFn)dlsym(sbs, "SBDimScreen");
      enableLock = (EnableLockFn)dlsym(sbs, "SBEnableLockScreenBundle");
      launchErrStr = (LaunchErrStrFn)dlsym(sbs, "SBSApplicationLaunchingErrorString");
    }
    if (gs) resetIdle = (ResetIdleFn)dlsym(gs, "GSEventResetIdleTimer");
    if (!sbs) why = "dlopen SpringBoardServices failed";
    else if (!launch) why = "SBSLaunchApplicationWithIdentifier missing";
  }
  bool wake(const std::string& bundleId) override {
    ObjcAutoreleasePool pool;  // B1.10: SBS/CF calls may autorelease
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
    // B1.15 A: decode the launch result. SBSApplicationLaunchingErrorString (if present) names the
    // code (0 ok, 1 "SpringBoard is not running" in the table but in practice the unentitled-caller
    // code, 3 device locked, 7 app not found, ...). On a non-zero result print the resolved string or
    // the number; on code 1 add the real cause we proved on the device: a missing entitlement.
    char err[160] = "";
    if (rc != 0) {
      char name[96] = "";
      if (launchErrStr) { CFStringRef m = launchErrStr(rc); if (m) { CFStringGetCString(m, name, sizeof name, kCFStringEncodingUTF8); } }
      snprintf(err, sizeof err, " (error %d%s%s%s)", rc, name[0] ? ": " : "", name,
               rc == 1 ? "; binary probably lacks com.apple.springboard.launchapplications" : "");
    }
    IOS3_TRACE("wake-on-play: SBDimScreen(false) -> %d, GSEventResetIdleTimer %s, SBSLaunchApplicationWithIdentifier(%s) -> %d%s", (int)kr,
               resetIdle ? "called" : "missing", bundleId.c_str(), rc, err);
    return rc == 0;
  }
  bool enableLockBundle(const std::string& name) override { return setLock(name, true); }
  bool disableLockBundle(const std::string& name) override { return setLock(name, false); }
  std::string describe() override {
    return launch ? std::string("SpringBoardServices (undim: ") + (dim && serverPort ? "SBDimScreen" : "missing") + ", idle reset: " + (resetIdle ? "yes" : "no") + ")"
                  : "SpringBoardServices unavailable: " + why;
  }

 private:
  bool setLock(const std::string& name, bool enable) {
    if (!enableLock || !serverPort) {
      if (!lockUnavailableLogged) { IOS3_TRACE("lock-plugin: SBEnableLockScreenBundle unavailable: no-op"); lockUnavailableLogged = true; }
      return false;
    }
    mach_port_t p = serverPort();
    if (!p) return false;
    kern_return_t kr = enableLock(p, enable ? 1 : 0, name.c_str());
    IOS3_TRACE("lock-plugin: SBEnableLockScreenBundle(%s, \"%s\") -> %d", enable ? "enable" : "disable", name.c_str(), (int)kr);
    return kr == 0;
  }
  ServerPortFn serverPort = nullptr;
  LaunchFn launch = nullptr;
  DimFn dim = nullptr;
  ResetIdleFn resetIdle = nullptr;
  EnableLockFn enableLock = nullptr;
  LaunchErrStrFn launchErrStr = nullptr;
  std::string why;
  bool unavailableLogged = false;
  bool lockUnavailableLogged = false;
};

}  // namespace

DisplayWaker* createSpringBoardWaker() { return new SpringBoardWaker(); }
