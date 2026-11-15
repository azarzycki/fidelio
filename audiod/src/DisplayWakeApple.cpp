// B1.9: wake the display and foreground the NowPlaying app via SpringBoardServices (private),
// iPhone OS 3.1. Symbols confirmed exported by the SDK's SpringBoardServices binary (nm):
//   SBSSpringBoardServerPort            mach_port_t (void)
//   SBSLaunchApplicationWithIdentifier  int (CFStringRef displayIdentifier, Boolean suspended)
//   SBDimScreen                         kern_return_t (mach_port_t serverPort, Boolean dim)   [MIG stub, 1 byte arg]
// and by GraphicsServices:
//   GSEventResetIdleTimer               void (void)
// There is no SBSUndimScreen in 3.1; SBDimScreen(port, false) is the undim call of that era (symbol
// confirmed, undim semantics assumed; verified on the device). A LOCKED iOS 5 device refuses the
// launch with 3 (verified on the iPad).
// B1.16.4: SBSOpenSensitiveURLAndUnlock  Boolean (CFURLRef url, Boolean unlock)
// Exported by the 3.1.2 SDK's SpringBoardServices too (so the phone has it). Disassembled there: it
// takes the URL's string (at most 1023 UTF-8 bytes) and sends SBOpenSensitiveURL(port, utf8, unlock) to
// SpringBoard. B1.16.6, verified on the locked 5.1.1 iPad: the result is SpringBoard's answer. A caller
// without com.apple.springboard.openurlswhenlocked gets false for nowplaying: (an app's own scheme;
// prefs: needs only opensensitiveurl). With all four keys of common/fidelio-springboard.entitlements.plist
// it returns true, and the iPad is unlocked with NowPlaying in front within 2 s. SpringBoard's isLocked
// read 0 at 2, 6 and 12 s, while SBSLaunchApplicationWithIdentifier kept answering 3 for 5 s. So the
// accepted request is the result, and no launch is retried after it.
#include <dlfcn.h>
#include <mach/mach.h>
#include <mach/mach_time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/sysctl.h>

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
typedef Boolean (*OpenUnlockFn)(CFURLRef, Boolean);  // B1.16.4: SBSOpenSensitiveURLAndUnlock

uint64_t monoMs() {
  static mach_timebase_info_data_t tb;
  if (tb.denom == 0) mach_timebase_info(&tb);
  return mach_absolute_time() / 1000000ull * tb.numer / tb.denom;
}

class SpringBoardWaker : public DisplayWaker {
 public:
  SpringBoardWaker() {
    void* sbs = dlopen("/System/Library/PrivateFrameworks/SpringBoardServices.framework/SpringBoardServices", RTLD_LAZY);
    void* gs = dlopen("/System/Library/PrivateFrameworks/GraphicsServices.framework/GraphicsServices", RTLD_LAZY);
    if (sbs) {
      serverPort = (ServerPortFn)dlsym(sbs, "SBSSpringBoardServerPort");
      launchFn = (LaunchFn)dlsym(sbs, "SBSLaunchApplicationWithIdentifier");
      dim = (DimFn)dlsym(sbs, "SBDimScreen");
      enableLock = (EnableLockFn)dlsym(sbs, "SBEnableLockScreenBundle");
      launchErrStr = (LaunchErrStrFn)dlsym(sbs, "SBSApplicationLaunchingErrorString");
      openUnlock = (OpenUnlockFn)dlsym(sbs, "SBSOpenSensitiveURLAndUnlock");
    }
    if (gs) resetIdle = (ResetIdleFn)dlsym(gs, "GSEventResetIdleTimer");
    if (!sbs) why = "dlopen SpringBoardServices failed";
    else if (!launchFn) why = "SBSLaunchApplicationWithIdentifier missing";
  }
  int wake(const std::string& bundleId) override {
    ObjcAutoreleasePool pool;  // B1.10: SBS/CF calls may autorelease
    if (!launchFn) {
      if (!unavailableLogged) { IOS3_TRACE("wake-on-play: unavailable (%s): no-op", why.c_str()); unavailableLogged = true; }
      return kUnavailable;
    }
    // 1. undim / wake the screen (best effort), 2. reset the idle timer, 3. foreground the app
    kern_return_t kr = undimAndResetIdle();
    char err[200];
    uint64_t ms = 0;
    int rc = launchDecoded(bundleId, err, sizeof err, &ms);
    IOS3_TRACE("wake-on-play: SBDimScreen(false) -> %d, GSEventResetIdleTimer %s, SBSLaunchApplicationWithIdentifier(%s) -> %d%s (%llu ms)", (int)kr,
               resetIdle ? "called" : "missing", bundleId.c_str(), rc, err, (unsigned long long)ms);
    return rc;
  }
  // B1.16.10: wake_mode=screen: steps 1 and 2 of wake(), no launch.
  void wakeScreen() override {
    ObjcAutoreleasePool pool;
    kern_return_t kr = undimAndResetIdle();
    IOS3_TRACE("wake-on-play (screen only): SBDimScreen(false) -> %d, GSEventResetIdleTimer %s", (int)kr, resetIdle ? "called" : "missing");
  }
  bool canUnlock() override { return openUnlock != nullptr; }
  int openURLAndUnlock(const std::string& url) override {
    ObjcAutoreleasePool pool;
    if (!openUnlock) return kUnavailable;
    CFURLRef u = CFURLCreateWithBytes(nullptr, (const UInt8*)url.data(), (CFIndex)url.size(), kCFStringEncodingUTF8, nullptr);
    if (!u) { IOS3_TRACE("unlock-on-play: '%s' is not a valid URL", url.c_str()); return 0; }
    uint64_t t0 = monoMs();
    Boolean ok = openUnlock(u, true);
    uint64_t ms = monoMs() - t0;
    CFRelease(u);
    IOS3_TRACE("unlock-on-play: SBSOpenSensitiveURLAndUnlock(%s, unlock) -> %s (%llu ms)", url.c_str(),
               ok ? "true (accepted: unlocks and opens the app)" : "false (refused, e.g. a missing entitlement)", (unsigned long long)ms);
    return ok ? 1 : 0;
  }
  // B1.16.11: SpringBoard's pid from the kernel's process list (0 if it is not running right now).
  int springBoardPid() override {
    int mib[4] = { CTL_KERN, KERN_PROC, KERN_PROC_ALL, 0 };
    size_t len = 0;
    if (sysctl(mib, 4, nullptr, &len, nullptr, 0) != 0 || len == 0) return 0;
    len += len / 4;  // the list may grow between the two calls
    struct kinfo_proc* procs = (struct kinfo_proc*)malloc(len);
    if (!procs) return 0;
    int pid = 0;
    if (sysctl(mib, 4, procs, &len, nullptr, 0) == 0) {
      for (size_t i = 0, n = len / sizeof(struct kinfo_proc); i < n; i++)
        if (strncmp(procs[i].kp_proc.p_comm, "SpringBoard", sizeof procs[i].kp_proc.p_comm) == 0) { pid = procs[i].kp_proc.p_pid; break; }
    }
    free(procs);
    return pid;
  }
  bool enableLockBundle(const std::string& name) override { return setLock(name, true); }
  bool disableLockBundle(const std::string& name) override { return setLock(name, false); }
  std::string describe() override {
    return launchFn ? std::string("SpringBoardServices (undim: ") + (dim && serverPort ? "SBDimScreen" : "missing") + ", idle reset: " + (resetIdle ? "yes" : "no") +
                          ", unlock: " + (openUnlock ? "SBSOpenSensitiveURLAndUnlock" : "missing") + ")"
                    : "SpringBoardServices unavailable: " + why;
  }

 private:
  // Undim the screen (best effort) and reset the idle timer: the part of the wake both modes share.
  kern_return_t undimAndResetIdle() {
    kern_return_t kr = KERN_SUCCESS;
    if (dim && serverPort) { mach_port_t p = serverPort(); if (p) kr = dim(p, 0); }
    if (resetIdle) resetIdle();
    return kr;
  }
  // SBSLaunchApplicationWithIdentifier + the B1.15 A decode of a non-zero result into err (" (error ...)").
  int launchDecoded(const std::string& bundleId, char* err, size_t errLen, uint64_t* ms) {
    CFStringRef id = CFStringCreateWithCString(nullptr, bundleId.c_str(), kCFStringEncodingUTF8);
    uint64_t t0 = monoMs();
    int rc = launchFn(id, 0);
    *ms = monoMs() - t0;
    CFRelease(id);
    // B1.15 A: decode the launch result. SBSApplicationLaunchingErrorString (if present) names the
    // code (0 ok, 1 "SpringBoard is not running" in the table but in practice the unentitled-caller
    // code, 3 device locked, 7 app not found, ...). On a non-zero result print the resolved string or
    // the number; on code 1 add the real cause we proved on the device: a missing entitlement.
    err[0] = 0;
    if (rc != 0) {
      char name[96] = "";
      if (launchErrStr) { CFStringRef m = launchErrStr(rc); if (m) { CFStringGetCString(m, name, sizeof name, kCFStringEncodingUTF8); } }
      snprintf(err, errLen, " (error %d%s%s%s)", rc, name[0] ? ": " : "", name,
               rc == 1 ? "; binary probably lacks com.apple.springboard.launchapplications" : "");
    }
    return rc;
  }
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
  LaunchFn launchFn = nullptr;
  DimFn dim = nullptr;
  ResetIdleFn resetIdle = nullptr;
  EnableLockFn enableLock = nullptr;
  LaunchErrStrFn launchErrStr = nullptr;
  OpenUnlockFn openUnlock = nullptr;
  std::string why;
  bool unavailableLogged = false;
  bool lockUnavailableLogged = false;
};

}  // namespace

DisplayWaker* createSpringBoardWaker() { return new SpringBoardWaker(); }
