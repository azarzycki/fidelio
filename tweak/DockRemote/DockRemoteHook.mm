// B1.16 part C3: the MobileSubstrate hook for DockRemote, wired to the iapd symbol supplied from the
// device. The Simple Remote keys arrive at -[SimpleRemote sendEventRecord:toApp:] (an Objective-C
// method on class SimpleRemote : PortStack, IMP 0xcb4c in the 3.1.3 iapd; hookable by selector, no
// address), which iapd calls for every accessory event with the target app. The record is a
// GSEventRecord; for a Simple Remote key it is type kGSEventAccessoryKeyStateChanged and the payload
// carries the key number (play/pause 0, next 3, prev 4, menu 22, select 23, up 24, down 25).
//
// STAGING (as requested): this ships in OBSERVE-ONLY mode. With the observe flag set it logs each
// record's type/subtype/infoSize and the first payload bytes in hex (so the exact key offset is
// confirmed on the device before anything is swallowed) and ALWAYS calls the original. Only when the
// separate swallow flag is ALSO set does it forward a mapped key (0/3/4) to audiod and swallow it; by
// default (no swallow flag) nothing is swallowed, so the dock behaves exactly as before. The global
// kill switch /var/mobile/fidelio/remote-tweak.disabled disables everything. A crash in iapd breaks
// the dock until a reseat, so every access is pointer-/size-checked and @try-guarded.
#import <Foundation/Foundation.h>
#import <objc/runtime.h>
#import <objc/message.h>
#import <dlfcn.h>
#import <sys/stat.h>
#import <syslog.h>

#import "DockRemoteCore.h"

// GSEventRecord, from the device's own type encoding for the method's 1st arg:
//   ^{?=iI{CGPoint=ff}{CGPoint=ff}IQ^viiI[0C]}
// i.e. int type; uint subtype; CGPoint location; CGPoint windowLocation; uint windowContextId;
//      uint64 timestamp; void* window; int; int; uint infoSize; char info[].
typedef struct {
  int type;
  unsigned subtype;
  float locX, locY;
  float winLocX, winLocY;
  unsigned windowContextId;
  unsigned long long timestamp;
  void* window;
  int i0;
  int i1;
  unsigned infoSize;
  unsigned char info[0];
} DRGSEventRecord;

static const char* kObserve = "/var/mobile/fidelio/remote-tweak.observe";  // log every record
static const char* kSwallow = "/var/mobile/fidelio/remote-tweak.swallow";  // actually intercept 0/3/4
static const char* kLog     = "/var/mobile/fidelio/remote-events.log";

typedef void (*MSHookMessageEx_t)(Class, SEL, IMP, IMP*);

// The original implementation we chain to. Signature: BOOL (-[SimpleRemote sendEventRecord:toApp:]).
static char (*orig_sendEventRecord)(id, SEL, void*, id) = 0;

static bool exists(const char* p) { struct stat st; return stat(p, &st) == 0; }

static void observeLog(NSString* line) {
  @try {
    FILE* f = fopen(kLog, "a");
    if (!f) return;
    fputs([line UTF8String], f);
    fputc('\n', f);
    fclose(f);
  } @catch (...) {}
}

static char new_sendEventRecord(id self, SEL _cmd, void* record, id app) {
  @try {
    bool observe = exists(kObserve);
    bool swallow = exists(kSwallow) && !exists("/var/mobile/fidelio/remote-tweak.disabled");
    int key = -1;
    const DRGSEventRecord* r = (const DRGSEventRecord*)record;
    if (r && r->infoSize >= 4) key = *(const int*)r->info;  // best-effort: the key is the first info int

    if (observe && r) {
      NSMutableString* hex = [NSMutableString string];
      unsigned n = r->infoSize; if (n > 32) n = 32;
      for (unsigned i = 0; i < n; i++) [hex appendFormat:@"%02x ", r->info[i]];
      NSString* a = @"?";
      @try { if (app) a = [app description]; } @catch (...) {}
      observeLog([NSString stringWithFormat:@"sendEventRecord type=%d subtype=%u infoSize=%u firstInt=%d app=%@ info=[%@]",
                                            r->type, r->subtype, r->infoSize, key, a, hex]);
    }

    if (swallow && key >= 0 && dockremote::verbForKey(key) && dockremote::shouldForward(key)) {
      uint8_t verb = dockremote::verbForKey(key);
      dockremote::forward(verb);
      if (observe) observeLog([NSString stringWithFormat:@"SWALLOWED key=%d -> audiod verb=%u (iPod app not notified)", key, verb]);
      return 1;  // tell iapd we handled it; do NOT call the original (the iPod app never sees it)
    }
  } @catch (...) {}
  // Default and safe path: behave exactly as iapd would.
  if (orig_sendEventRecord) return orig_sendEventRecord(self, _cmd, record, app);
  return 0;
}

__attribute__((constructor))
static void DockRemoteInit() {
  @try {
    void* sub = dlopen("/Library/MobileSubstrate/MobileSubstrate.dylib", RTLD_LAZY);
    if (!sub) sub = dlopen("/usr/lib/libsubstrate.dylib", RTLD_LAZY);
    MSHookMessageEx_t MSHookMessageEx = sub ? (MSHookMessageEx_t)dlsym(sub, "MSHookMessageEx") : 0;
    Class cls = objc_getClass("SimpleRemote");
    if (MSHookMessageEx && cls) {
      MSHookMessageEx(cls, @selector(sendEventRecord:toApp:), (IMP)new_sendEventRecord, (IMP*)&orig_sendEventRecord);
      syslog(LOG_NOTICE, "DockRemote: hooked -[SimpleRemote sendEventRecord:toApp:] (observe-only until the swallow flag is set)");
    } else {
      syslog(LOG_NOTICE, "DockRemote: NOT hooked (substrate %s, SimpleRemote %s) — safe no-op",
             sub ? "found" : "missing", cls ? "found" : "missing");
    }
  } @catch (...) {}
}
