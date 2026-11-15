// B1.16 part C3: the MobileSubstrate hook for DockRemote. This is the ONLY symbol-dependent part and
// is intentionally left with the hook target UNWIRED until the iapd Simple-Remote symbol is supplied
// from the device (see tweak/DockRemote/README.md: the nm / otool -oV / strings dump of
// /System/Library/PrivateFrameworks/IAP.framework/Support/iapd). The symbol-independent logic lives in
// DockRemoteCore. Do NOT install this into iapd until the hook is wired and reasoned about: a crash in
// iapd breaks the dock connection until the phone is physically reseated.
//
// Expected wiring once the symbol is known (two candidate shapes):
//   (a) iapd dispatches the key through an Objective-C method (e.g. a SimpleRemoteEventHandler ObjC
//       class, or -[AccessoryManager ...]). Then: MSHookMessageEx(cls, @selector(...), &New, &Orig),
//       read the key from the args, and on dockremote::shouldForward(key) call dockremote::forward()
//       and RETURN WITHOUT calling Orig (swallow); otherwise call Orig (fall through).
//   (b) It is an unexported C++ method ("SimpleRemoteEventHandler::...(evKeyDown, key)"). Then:
//       MSHookFunction(symbolAddress, (void*)&New, (void**)&Orig), same decision inside.
// Keep press-and-hold intact on fall-through by NOT consuming the key unless we actually forward it.
#import <dlfcn.h>
#import <syslog.h>

#import "DockRemoteCore.h"

// MobileSubstrate is on the device, not in the SDK: resolve it at load time.
typedef void (*MSHookFunction_t)(void* symbol, void* replace, void** result);
typedef void (*MSHookMessageEx_t)(Class cls, SEL sel, void* imp, void** result);
static MSHookFunction_t pMSHookFunction = 0;
static MSHookMessageEx_t pMSHookMessageEx = 0;

// The decision a wired hook calls for each Simple Remote context key. Returns true if the key was
// consumed (forwarded to audiod); false if the caller should run the original handler.
extern "C" bool DockRemoteHandleKey(int key) {
  @try {
    if (!dockremote::shouldForward(key)) return false;
    uint8_t verb = dockremote::verbForKey(key);
    dockremote::forward(verb);
    return true;  // consumed: the iPod app must not also act on it
  } @catch (...) { return false; }  // any doubt: fall through
}

__attribute__((constructor))
static void DockRemoteInit() {
  void* sub = dlopen("/Library/MobileSubstrate/MobileSubstrate.dylib", RTLD_LAZY);
  if (!sub) sub = dlopen("/usr/lib/libsubstrate.dylib", RTLD_LAZY);
  if (sub) {
    pMSHookFunction = (MSHookFunction_t)dlsym(sub, "MSHookFunction");
    pMSHookMessageEx = (MSHookMessageEx_t)dlsym(sub, "MSHookMessageEx");
  }
  // TODO(device symbol): install the hook on iapd's Simple Remote key dispatcher here, per the shape
  // (a)/(b) above, routing each key through DockRemoteHandleKey(). Until then the tweak loads but does
  // nothing (safe no-op), so it can be shipped without risk once the symbol is filled in.
  syslog(LOG_NOTICE, "DockRemote loaded (substrate %s); hook NOT wired yet (awaiting iapd Simple-Remote symbol)",
         (pMSHookFunction || pMSHookMessageEx) ? "found" : "missing");
}
