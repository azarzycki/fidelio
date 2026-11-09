// B1.9: the device output volume through the private AVSystemController (Celestial.framework),
// iPhone OS 3.1. Everything is resolved at runtime (dlopen / objc_getClass / sel_registerName), so a
// missing framework, class or selector only makes the backend unavailable.
//
// Verified against the SDK binaries (strings / nm of iPhoneOS3.1.2.sdk Celestial, MediaPlayer):
//   +[AVSystemController sharedAVSystemController]           (present)
//   -[AVSystemController setVolumeTo:forCategory:]           (present; MediaPlayer calls it with "Audio/Video")
//   -[AVSystemController getVolume:forCategory:]             (present)
//   AVSystemController_SystemVolumeDidChangeNotification     (present), with the userInfo keys
//   AVSystemController_AudioVolumeNotificationParameter and AVSystemController_AudioCategoryNotificationParameter
// Not verifiable off-device: whether the notification is delivered to a daemon with no main run
// loop (we observe it on CF's local center AND poll getVolume: every second as the fallback).
#include <dlfcn.h>
#include <objc/message.h>
#include <objc/runtime.h>
#include <stdio.h>

#include <CoreFoundation/CoreFoundation.h>

#include "HardwareVolume.h"
#include "ios3_trace.h"

namespace {

typedef id (*MsgSendId)(id, SEL);
typedef signed char (*MsgSendGetVolume)(id, SEL, float*, id);
typedef signed char (*MsgSendSetVolume)(id, SEL, float, id);

class AVSystemControllerVolume : public HardwareVolume {
 public:
  AVSystemControllerVolume() { load(); }
  ~AVSystemControllerVolume() override {
    if (observing) CFNotificationCenterRemoveEveryObserver(CFNotificationCenterGetLocalCenter(), this);
    if (category) CFRelease(category);
  }
  bool available() override { return ok; }
  bool get(float& level) override {
    if (!ok) return false;
    float v = 0;
    signed char r = ((MsgSendGetVolume)objc_msgSend)(controller, selGet, &v, (id)category);
    if (!r) { if (!getFailedLogged) { IOS3_TRACE("AVSystemController getVolume:forCategory: returned NO"); getFailedLogged = true; } return false; }
    level = v;
    lastKnown = v;
    return true;
  }
  bool set(float level) override {
    if (!ok) return false;
    signed char r = ((MsgSendSetVolume)objc_msgSend)(controller, selSet, level, (id)category);
    if (r) lastKnown = level;
    else IOS3_TRACE("AVSystemController setVolumeTo:%.3f forCategory:Audio/Video returned NO", level);
    return r != 0;
  }
  bool takeChange(float& level) override {
    if (!pending) return false;
    pending = false;
    level = pendingLevel;
    return true;
  }
  std::string describe() override {
    return ok ? std::string("AVSystemController (Celestial, category Audio/Video, notification ") + (observing ? "observed" : "not observed") + " + 1 s poll)"
              : "AVSystemController unavailable: " + why;
  }

 private:
  void load() {
    void* h = dlopen("/System/Library/PrivateFrameworks/Celestial.framework/Celestial", RTLD_LAZY);
    if (!h) { why = std::string("dlopen Celestial: ") + (dlerror() ? dlerror() : "?"); return; }
    Class cls = (Class)objc_getClass("AVSystemController");  // the 3.1 SDK declares it as returning id
    if (!cls) { why = "class AVSystemController not found"; return; }
    SEL selShared = sel_registerName("sharedAVSystemController");
    selGet = sel_registerName("getVolume:forCategory:");
    selSet = sel_registerName("setVolumeTo:forCategory:");
    if (!class_getClassMethod(cls, selShared)) { why = "+sharedAVSystemController missing"; return; }
    if (!class_getInstanceMethod(cls, selGet) || !class_getInstanceMethod(cls, selSet)) { why = "getVolume:forCategory: / setVolumeTo:forCategory: missing"; return; }
    controller = ((MsgSendId)objc_msgSend)((id)cls, selShared);
    if (!controller) { why = "sharedAVSystemController returned nil"; return; }
    category = CFStringCreateWithCString(nullptr, "Audio/Video", kCFStringEncodingUTF8);
    float v = 0;
    if (!((MsgSendGetVolume)objc_msgSend)(controller, selGet, &v, (id)category)) { why = "getVolume:forCategory:Audio/Video returned NO"; return; }
    lastKnown = v;
    ok = true;
    CFNotificationCenterAddObserver(CFNotificationCenterGetLocalCenter(), this, &AVSystemControllerVolume::onNotification,
                                    CFSTR("AVSystemController_SystemVolumeDidChangeNotification"), nullptr, CFNotificationSuspensionBehaviorDeliverImmediately);
    observing = true;
  }
  static void onNotification(CFNotificationCenterRef, void* observer, CFStringRef, const void*, CFDictionaryRef userInfo) {
    AVSystemControllerVolume* self = static_cast<AVSystemControllerVolume*>(observer);
    if (!userInfo) return;
    CFStringRef cat = (CFStringRef)CFDictionaryGetValue(userInfo, CFSTR("AVSystemController_AudioCategoryNotificationParameter"));
    if (cat && CFGetTypeID(cat) == CFStringGetTypeID() && CFStringCompare(cat, CFSTR("Audio/Video"), 0) != kCFCompareEqualTo) return;
    CFNumberRef n = (CFNumberRef)CFDictionaryGetValue(userInfo, CFSTR("AVSystemController_AudioVolumeNotificationParameter"));
    if (!n || CFGetTypeID(n) != CFNumberGetTypeID()) return;
    float v = 0;
    if (!CFNumberGetValue(n, kCFNumberFloatType, &v)) return;
    self->pendingLevel = v;
    self->pending = true;  // picked up by the Server's next tick (takeChange)
  }

  bool ok = false, observing = false, getFailedLogged = false;
  std::string why;
  id controller = nullptr;
  SEL selGet = nullptr, selSet = nullptr;
  CFStringRef category = nullptr;
  float lastKnown = 0;
  volatile bool pending = false;
  volatile float pendingLevel = 0;
};

}  // namespace

HardwareVolume* createAVSystemControllerVolume() { return new AVSystemControllerVolume(); }
