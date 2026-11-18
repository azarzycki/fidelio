// voltest: a device probe for the system volume (B1.16.12 diagnosis). It asks Celestial's private
// AVSystemController the questions audiod's absolute volume depends on.
//  - Which audio category is active while audiod plays, and what are its volume and the Audio/Video and
//    MediaPlayback volumes? audiod sets and reads "Audio/Video".
//  - Which volume category does each audio category map to?
//  - Which route does each category play to, and does the current route have volume control at all?
// It can also set one volume, so each setter can be tried by ear. On a docked phone the Spotify slider
// (audiod: setVolumeTo:forCategory:@"Audio/Video", which returns YES) moved the display but not the
// dock, while the dock's buttons did.
//
// Every selector is checked at run time, and its type encoding must match before it is called (a
// private API with a guessed signature is never called blindly). Run it over SSH as root, with audiod
// playing:
//   voltest                    show the state
//   voltest set active V       setActiveCategoryVolumeTo:V           (what the volume buttons change)
//   voltest set CATEGORY V     setVolumeTo:V forCategory:CATEGORY    (e.g. Audio/Video = what audiod does)
//   voltest watch [SECONDS]    print the state whenever it changes (5 polls a second; default 30 s)
//
// The accessory daemon's side (iapd, reached through the private IAP.framework). In iapd-444.1 the only
// way the phone's volume goes to a dock is a Display Remote volume notification, and iapd sends it only
// after IAPAudioSetVolume, and only if the accessory asked for volume events:
//   voltest iap                show only iapd's side
//   voltest iap set V          IAPAudioSetVolume(V), 0..1: ask iapd to send the volume to the accessory.
//                              START LOW: if the dock follows, this sets its loudness.
//   voltest iap offset N       IAPDigitalAudioSetVolumeOffset(N): the volume field of the Digital Audio
//                              lingo's track attributes (units unknown; try small negative numbers)
// Every state line ends with "iap: supported= volume= lineout= digital=": whether the accessory has
// volume control, iapd's own volume (0..1), and the line-out and digital-audio device states. If
// iapd's volume follows "voltest set Audio/Video V", the system forwards the phone's volume to iapd.
// If it only follows the dock's buttons, nothing forwards it and audiod has to call IAPAudioSetVolume.
// iPhone OS 3.1 SDK, armv6: one binary for both devices.
#import <Foundation/Foundation.h>
#import <objc/message.h>
#import <objc/runtime.h>

#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static id ctl = nil;

// The method's type encoding, or NULL if the controller does not respond to it.
static const char* Types(const char* sel) {
  Method m = class_getInstanceMethod(object_getClass(ctl), sel_registerName(sel));
  return m ? method_getTypeEncoding(m) : NULL;
}
// The encoding must start with the expected return type (the rest is offsets and argument types).
static BOOL Returns(const char* sel, char ret) {
  const char* t = Types(sel);
  return t && t[0] == ret;
}
static NSString* Desc(id o) {
  if (!o) return @"(nil)";
  NSString* d = [o description];
  return [[d componentsSeparatedByCharactersInSet:[NSCharacterSet newlineCharacterSet]] componentsJoinedByString:@" "];
}

// ---- iapd's side: IAP.framework's C wrappers around MIG calls to the com.apple.iapd service. They are
// plain C exports, so there is no type encoding to check at run time. These prototypes were read from
// the armv6 code of the 3.1.2 SDK's IAP binary: every wrapper returns 1 when its MIG call succeeded;
// IAPAudioSetVolume takes the float in r0 (softfp); IAPAudioIsVolumeControlSupported stores one byte,
// IAPAudioGetVolume a float and IAPAudioGetDeviceState an int through the pointer.
typedef BOOL (*IapSetVolumeFn)(float);
typedef BOOL (*IapGetVolumeFn)(float*);
typedef BOOL (*IapGetBoolFn)(BOOL*);
typedef BOOL (*IapGetDeviceStateFn)(int, int*);
typedef BOOL (*IapSetIntFn)(int);
static void* iap = NULL;
static void* IapSym(const char* name) { return iap ? dlsym(iap, name) : NULL; }

static NSString* IapState(void) {
  if (!iap) return @"iap: unavailable";
  NSMutableString* s = [NSMutableString stringWithString:@"iap:"];
  IapGetBoolFn sup = (IapGetBoolFn)IapSym("IAPAudioIsVolumeControlSupported");
  BOOL b = NO;
  if (!sup) [s appendString:@" supported=missing"];
  else if (sup(&b)) [s appendFormat:@" supported=%d", (int)b];
  else [s appendString:@" supported=failed"];
  IapGetVolumeFn get = (IapGetVolumeFn)IapSym("IAPAudioGetVolume");
  float v = -1;
  if (!get) [s appendString:@" volume=missing"];
  else if (get(&v)) [s appendFormat:@" volume=%.3f", v];
  else [s appendString:@" volume=failed"];
  // Device ids as iapd uses them: 1 = line-out, 3 = digital audio.
  IapGetDeviceStateFn dev = (IapGetDeviceStateFn)IapSym("IAPAudioGetDeviceState");
  static const int ids[2] = {1, 3};
  static const char* const names[2] = {"lineout", "digital"};
  for (int i = 0; i < 2; i++) {
    int st = -1;
    if (!dev) [s appendFormat:@" %s=missing", names[i]];
    else if (dev(ids[i], &st)) [s appendFormat:@" %s=%d", names[i], st];
    else [s appendFormat:@" %s=failed", names[i]];
  }
  return s;
}
// iapd also serves the dock, so "watch" asks it only on every iapEvery-th state line.
static int iapEvery = 1;
static NSString* IapStateThrottled(void) {
  static NSString* cached = nil;
  static int n = 0;
  if (!cached || n++ % iapEvery == 0) {
    [cached release];
    cached = [IapState() retain];
  }
  return cached;
}

static NSString* State(void) {
  NSMutableString* s = [NSMutableString string];
  float v = -1;
  id name = nil;
  if (Returns("getActiveCategoryVolume:andName:", 'c')) {
    BOOL ok = ((BOOL (*)(id, SEL, float*, id*))objc_msgSend)(ctl, sel_registerName("getActiveCategoryVolume:andName:"), &v, &name);
    [s appendFormat:@"active=%@ %.3f%@", Desc(name), v, ok ? @"" : @" (returned NO)"];
  } else [s appendString:@"active=? (no getActiveCategoryVolume:andName:)"];
  NSArray* cats = [NSArray arrayWithObjects:@"Audio/Video", @"MediaPlayback", nil];
  for (NSString* c in cats) {
    v = -1;
    BOOL ok = Returns("getVolume:forCategory:", 'c') &&
              ((BOOL (*)(id, SEL, float*, id))objc_msgSend)(ctl, sel_registerName("getVolume:forCategory:"), &v, c);
    [s appendFormat:@" | %@=%.3f%@", c, v, ok ? @"" : @"(NO)"];
    if (Returns("volumeCategoryForAudioCategory:", '@'))
      [s appendFormat:@" vcat=%@", Desc(((id (*)(id, SEL, id))objc_msgSend)(ctl, sel_registerName("volumeCategoryForAudioCategory:"), c))];
    if (Returns("routeForCategory:", '@'))
      [s appendFormat:@" route=%@", Desc(((id (*)(id, SEL, id))objc_msgSend)(ctl, sel_registerName("routeForCategory:"), c))];
  }
  if (Returns("currentRouteHasVolumeControl", 'c'))
    [s appendFormat:@" | routeHasVolumeControl=%d", (int)((BOOL (*)(id, SEL))objc_msgSend)(ctl, sel_registerName("currentRouteHasVolumeControl"))];
  if (Returns("attributeForKey:", '@')) {
    NSArray* keys = [NSArray arrayWithObjects:@"AVSystemController_CurrentlyActiveCategoryAttribute", @"AVSystemController_AudioIsPlayingSomewhereAttribute",
                                              @"AVSystemController_HeadphoneVolumeLimitAttribute", nil];
    for (NSString* k in keys) {
      id a = ((id (*)(id, SEL, id))objc_msgSend)(ctl, sel_registerName("attributeForKey:"), k);
      [s appendFormat:@" | %@=%@", [k stringByReplacingOccurrencesOfString:@"AVSystemController_" withString:@""], Desc(a)];
    }
  }
  [s appendFormat:@" | %@", IapStateThrottled()];
  return s;
}

static void Usage(void) {
  fprintf(stderr, "usage: voltest | voltest set active V | voltest set CATEGORY V | voltest watch [SECONDS]\n"
                  "       voltest iap | voltest iap set V | voltest iap offset N\n");
}

int main(int argc, char** argv) {
  NSAutoreleasePool* pool = [[NSAutoreleasePool alloc] init];
  if (!dlopen("/System/Library/PrivateFrameworks/Celestial.framework/Celestial", RTLD_LAZY)) { fprintf(stderr, "dlopen Celestial failed\n"); return 2; }
  Class cls = objc_getClass("AVSystemController");
  if (!cls || !class_getClassMethod(cls, sel_registerName("sharedAVSystemController"))) { fprintf(stderr, "AVSystemController unavailable\n"); return 2; }
  ctl = ((id (*)(id, SEL))objc_msgSend)((id)cls, sel_registerName("sharedAVSystemController"));
  if (!ctl) { fprintf(stderr, "sharedAVSystemController returned nil\n"); return 2; }
  iap = dlopen("/System/Library/PrivateFrameworks/IAP.framework/IAP", RTLD_LAZY);  // optional: the state line says so

  if (argc >= 2 && strcmp(argv[1], "iap") == 0) {
    if (!iap) { fprintf(stderr, "dlopen IAP failed\n"); return 2; }
    if (argc == 2) {
      printf("%s\n", [IapState() UTF8String]);
    } else if (argc == 4 && strcmp(argv[2], "set") == 0) {
      float v = (float)atof(argv[3]);
      if (v < 0 || v > 1) { fprintf(stderr, "V must be 0..1\n"); return 1; }
      IapSetVolumeFn set = (IapSetVolumeFn)IapSym("IAPAudioSetVolume");
      if (!set) { fprintf(stderr, "IAPAudioSetVolume missing\n"); return 2; }
      printf("before: %s\n", [State() UTF8String]);
      BOOL ok = set(v);
      printf("IAPAudioSetVolume(%.3f) -> %d\n", v, (int)ok);
      usleep(500 * 1000);
      printf("after:  %s\n", [State() UTF8String]);
    } else if (argc == 4 && strcmp(argv[2], "offset") == 0) {
      int n = atoi(argv[3]);
      IapSetIntFn off = (IapSetIntFn)IapSym("IAPDigitalAudioSetVolumeOffset");
      if (!off) { fprintf(stderr, "IAPDigitalAudioSetVolumeOffset missing\n"); return 2; }
      BOOL ok = off(n);
      printf("IAPDigitalAudioSetVolumeOffset(%d) -> %d\n", n, (int)ok);
    } else {
      Usage();
      return 1;
    }
  } else if (argc == 1) {
    printf("%s\n", [State() UTF8String]);
    printf("types: getActiveCategoryVolume:andName: %s | setActiveCategoryVolumeTo: %s | setVolumeTo:forCategory: %s | routeForCategory: %s | volumeCategoryForAudioCategory: %s\n",
           Types("getActiveCategoryVolume:andName:") ?: "-", Types("setActiveCategoryVolumeTo:") ?: "-", Types("setVolumeTo:forCategory:") ?: "-",
           Types("routeForCategory:") ?: "-", Types("volumeCategoryForAudioCategory:") ?: "-");
  } else if (argc == 4 && strcmp(argv[1], "set") == 0) {
    float v = (float)atof(argv[3]);
    if (v < 0 || v > 1) { fprintf(stderr, "V must be 0..1\n"); return 1; }
    printf("before: %s\n", [State() UTF8String]);
    BOOL ok = NO;
    if (strcmp(argv[2], "active") == 0) {
      if (!Returns("setActiveCategoryVolumeTo:", 'c')) { fprintf(stderr, "setActiveCategoryVolumeTo: missing or unexpected types\n"); return 2; }
      ok = ((BOOL (*)(id, SEL, float))objc_msgSend)(ctl, sel_registerName("setActiveCategoryVolumeTo:"), v);
      printf("setActiveCategoryVolumeTo:%.3f -> %d\n", v, (int)ok);
    } else {
      if (!Returns("setVolumeTo:forCategory:", 'c')) { fprintf(stderr, "setVolumeTo:forCategory: missing or unexpected types\n"); return 2; }
      NSString* cat = [NSString stringWithUTF8String:argv[2]];
      ok = ((BOOL (*)(id, SEL, float, id))objc_msgSend)(ctl, sel_registerName("setVolumeTo:forCategory:"), v, cat);
      printf("setVolumeTo:%.3f forCategory:%s -> %d\n", v, argv[2], (int)ok);
    }
    usleep(300 * 1000);
    printf("after:  %s\n", [State() UTF8String]);
  } else if (argc >= 2 && strcmp(argv[1], "watch") == 0) {
    int secs = argc >= 3 ? atoi(argv[2]) : 30;
    iapEvery = 5;  // iapd once a second
    NSString* last = nil;
    for (int i = 0; i < secs * 5; i++) {
      NSAutoreleasePool* p = [[NSAutoreleasePool alloc] init];
      NSString* now = State();
      if (!last || ![now isEqualToString:last]) {
        printf("%6.1fs %s\n", i / 5.0, [now UTF8String]);
        fflush(stdout);
        [last release];
        last = [now retain];
      }
      [p release];
      usleep(200 * 1000);
    }
    [last release];
  } else {
    Usage();
    return 1;
  }
  [pool release];
  return 0;
}
