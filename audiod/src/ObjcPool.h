#pragma once
// B1.10: audiod is a long-lived daemon with no main run loop and no app autorelease pool, so every
// ObjC/CoreFoundation call that autoreleases an object leaks it ("autoreleased with no pool in place
// - just leaking") -- and the hardware-volume getter is polled once a second. This RAII pool wraps
// every ObjC call site (HardwareVolumeApple, DisplayWakeApple, the notification callback). It is
// resolved through the runtime so it adds no link dependency; NSAutoreleasePool is in Foundation,
// which is already loaded by the time we dlopen Celestial / SpringBoardServices, so objc_getClass
// succeeds at these call sites. If it somehow does not, `pool` is nil and we simply behave as before.
#include <dlfcn.h>
#include <objc/message.h>
#include <objc/runtime.h>

class ObjcAutoreleasePool {
 public:
  ObjcAutoreleasePool() {
    Class c = (Class)objc_getClass("NSAutoreleasePool");
    if (!c) {
      // B1.10 follow-up: NSAutoreleasePool lives in Foundation, which audiod does not link. Before the
      // first dlopen of Celestial/SpringBoardServices pulls it in, the class is absent and startup
      // autoreleases ("no pool in place") leak. Load Foundation so a pool exists from the very first call.
      dlopen("/System/Library/Frameworks/Foundation.framework/Foundation", RTLD_LAZY);
      c = (Class)objc_getClass("NSAutoreleasePool");
    }
    if (!c) { pool_ = nullptr; return; }
    id alloc = ((id (*)(id, SEL))objc_msgSend)((id)c, sel_registerName("alloc"));
    pool_ = ((id (*)(id, SEL))objc_msgSend)(alloc, sel_registerName("init"));
  }
  ~ObjcAutoreleasePool() {
    if (pool_) ((void (*)(id, SEL))objc_msgSend)(pool_, sel_registerName("drain"));
  }
  ObjcAutoreleasePool(const ObjcAutoreleasePool&) = delete;
  ObjcAutoreleasePool& operator=(const ObjcAutoreleasePool&) = delete;

 private:
  id pool_;
};
