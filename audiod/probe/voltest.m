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
  return s;
}

static void Usage(void) {
  fprintf(stderr, "usage: voltest | voltest set active V | voltest set CATEGORY V | voltest watch [SECONDS]\n");
}

int main(int argc, char** argv) {
  NSAutoreleasePool* pool = [[NSAutoreleasePool alloc] init];
  if (!dlopen("/System/Library/PrivateFrameworks/Celestial.framework/Celestial", RTLD_LAZY)) { fprintf(stderr, "dlopen Celestial failed\n"); return 2; }
  Class cls = objc_getClass("AVSystemController");
  if (!cls || !class_getClassMethod(cls, sel_registerName("sharedAVSystemController"))) { fprintf(stderr, "AVSystemController unavailable\n"); return 2; }
  ctl = ((id (*)(id, SEL))objc_msgSend)((id)cls, sel_registerName("sharedAVSystemController"));
  if (!ctl) { fprintf(stderr, "sharedAVSystemController returned nil\n"); return 2; }

  if (argc == 1) {
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
