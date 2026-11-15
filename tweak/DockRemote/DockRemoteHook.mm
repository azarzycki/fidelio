// DockRemote, the MobileSubstrate hook inside iapd (iPhone OS 3.1.3 phone only).
//
// B1.16.8: the hook point is GraphicsServices' GSSendEvent, not -[SimpleRemote sendEventRecord:toApp:].
// Device findings (cspot-build-local):
//  - b051bfc bound to that method, but remote-events.log stayed empty for all seven keys, asleep and
//    awake: iapd's C++ SimpleRemoteEventHandler never calls it for keys.
//  - iapd imports _GSSendEvent and _GSSendSystemEvent, but not GSEventAccessoryKeyStateChanged.
//  - In the 3.1.2 SDK's GraphicsServices (armv6, disassembled), an accessory key is a 60-byte
//    GSEventRecord of type 4001 (0x0FA1): flags @40, infoSize @48 = 8, key (uint16) @52, state (int) @56.
//    It is sent with GSSendSystemEvent, which is GSSendEvent(record, the purple system event port).
//  So every key iapd hands to SpringBoard passes GSSendEvent, which MSHookFunction patches in place:
//  calls from inside GraphicsServices are seen too.
// Not used on the 5.1.1 iPad: its iapd imports no GSSendEvent and routes the keys with
// MRMediaRemoteSendCommand to the now-playing app, so that is an app-level route (NowPlaying), not a tweak.
//
// STAGING (unchanged policy):
//  - With no flags the hook is a pure pass-through.
//  - /var/mobile/fidelio/remote-tweak.observe: log every record iapd sends (still passed through).
//  - /var/mobile/fidelio/remote-tweak.swallow (as well): forward a mapped key (play/pause 0, next 3,
//    prev 4) to audiod and drop the record. Its key-down record is forwarded, both down and up are
//    dropped, and only when DockRemoteCore's shouldForward() agrees (setting on, a Fidelio source
//    active, audiod reachable).
//  - /var/mobile/fidelio/remote-tweak.disabled: a complete no-op, checked on every record.
//  A crash in iapd breaks the dock until a reseat. Fields are therefore read with memcpy at fixed
//  offsets, the info dump is bounded, and nothing blocks beyond DockRemoteCore's short socket timeouts.
#import <Foundation/Foundation.h>
#import <dlfcn.h>
#import <mach/mach.h>
#import <string.h>
#import <sys/stat.h>
#import <syslog.h>

#import "DockRemoteCore.h"

static const char* kObserve = "/var/mobile/fidelio/remote-tweak.observe";
static const char* kSwallow = "/var/mobile/fidelio/remote-tweak.swallow";
static const char* kDisable = "/var/mobile/fidelio/remote-tweak.disabled";
static const char* kLog     = "/var/mobile/fidelio/remote-events.log";

// The GSEventRecord header (iPhone OS 3.1, 52 bytes; the device's own type encoding for it was
// ^{?=iI{CGPoint=ff}{CGPoint=ff}IQ^viiI[0C]}), as byte offsets.
enum { OFF_TYPE = 0, OFF_SUBTYPE = 4, OFF_FLAGS = 40, OFF_INFOSIZE = 48, HEADER = 52 };
enum { GS_ACCESSORY_KEY_STATE = 4001, KEY_INFO_SIZE = 8, OFF_KEY = 52, OFF_STATE = 56 };
enum { KEY_STATE_DOWN = 1 };  // assumed; the observe log shows the real values before any swallow

typedef int (*GSSendEvent_t)(const void* record, mach_port_t port);
typedef void (*MSHookFunction_t)(void* symbol, void* replace, void** result);
static GSSendEvent_t orig_GSSendEvent = 0;

static bool exists(const char* p) { struct stat st; return stat(p, &st) == 0; }
static uint32_t rd32(const unsigned char* p, unsigned off) { uint32_t v; memcpy(&v, p + off, 4); return v; }
static uint16_t rd16(const unsigned char* p, unsigned off) { uint16_t v; memcpy(&v, p + off, 2); return v; }

static void logLine(const char* line) {
  FILE* f = fopen(kLog, "a");
  if (!f) return;
  fputs(line, f);
  fputc('\n', f);
  fclose(f);
}

static void hex(char* out, size_t cap, const unsigned char* p, unsigned n) {
  size_t used = 0;
  out[0] = 0;
  for (unsigned i = 0; i < n && used + 4 < cap; i++) used += snprintf(out + used, cap - used, "%02x%s", p[i], (i + 1) % 4 ? "" : " ");
}

static int new_GSSendEvent(const void* record, mach_port_t port) {
  @try {
    if (record && !exists(kDisable)) {
      const unsigned char* p = (const unsigned char*)record;
      int type = (int)rd32(p, OFF_TYPE);
      unsigned subtype = rd32(p, OFF_SUBTYPE), flags = rd32(p, OFF_FLAGS), infoSize = rd32(p, OFF_INFOSIZE);
      bool isKey = type == GS_ACCESSORY_KEY_STATE && infoSize == KEY_INFO_SIZE;  // proves the 3.1 layout
      int key = isKey ? (int)rd16(p, OFF_KEY) : -1;
      int state = isKey ? (int)rd32(p, OFF_STATE) : 0;
      if (exists(kObserve)) {
        char h[200], in[120], line[460];
        unsigned n = infoSize <= 32 ? infoSize : 32;  // a sane info size: dump at most 32 bytes of it
        hex(h, sizeof h, p, HEADER);
        hex(in, sizeof in, p + HEADER, infoSize <= 4096 ? n : 0);
        if (isKey) snprintf(line, sizeof line, "gs type=%d subtype=%u flags=0x%x infoSize=%u port=%u KEY key=%d state=%d hdr=[%s] info=[%s]",
                            type, subtype, flags, infoSize, (unsigned)port, key, state, h, in);
        else snprintf(line, sizeof line, "gs type=%d subtype=%u flags=0x%x infoSize=%u port=%u hdr=[%s] info=[%s]", type, subtype, flags, infoSize,
                      (unsigned)port, h, in);
        logLine(line);
      }
      if (isKey && exists(kSwallow) && dockremote::verbForKey(key) && dockremote::shouldForward(key)) {
        if (state == KEY_STATE_DOWN) {
          uint8_t verb = dockremote::verbForKey(key);
          bool sent = dockremote::forward(verb);
          if (exists(kObserve)) {
            char line[120];
            snprintf(line, sizeof line, "SWALLOWED key=%d state=%d -> audiod verb=%u (%s)", key, state, verb, sent ? "sent" : "send failed");
            logLine(line);
          }
        }
        return KERN_SUCCESS;  // dropped: SpringBoard (and the iPod app) never see this key
      }
    }
  } @catch (NSException* e) {}
  // Default and safe path: exactly what iapd asked for.
  return orig_GSSendEvent ? orig_GSSendEvent(record, port) : KERN_FAILURE;
}

// B1.16.5: substrate's functions live in CydiaSubstrate (/usr/lib/libsubstrate.dylib); on the phone
// MobileSubstrate.dylib is only the injection stub and exports no MSHook*. Use the first image that
// EXPORTS the symbol, not the first that opens.
static void* findSubstrate(const char* sym, const char** where) {
  void* f = dlsym(RTLD_DEFAULT, sym);
  if (f) { *where = "an image already loaded"; return f; }
  static const char* const libs[] = {
    "/usr/lib/libsubstrate.dylib",
    "/Library/Frameworks/CydiaSubstrate.framework/CydiaSubstrate",
    "/Library/MobileSubstrate/MobileSubstrate.dylib",
  };
  for (unsigned i = 0; i < sizeof libs / sizeof libs[0]; i++) {
    void* h = dlopen(libs[i], RTLD_LAZY);  // kept open: the hook lives in it
    if (!h) continue;
    f = dlsym(h, sym);
    if (f) { *where = libs[i]; return f; }
  }
  *where = "none of the substrate libraries";
  return 0;
}

__attribute__((constructor))
static void DockRemoteInit() {
  @try {
    const char* where = "";
    char line[300];
    MSHookFunction_t hook = (MSHookFunction_t)findSubstrate("MSHookFunction", &where);
    void* target = dlsym(RTLD_DEFAULT, "GSSendEvent");
    if (hook && target) {
      hook(target, (void*)new_GSSendEvent, (void**)&orig_GSSendEvent);
      snprintf(line, sizeof line, "DockRemote B1.16.8: hooked GSSendEvent with MSHookFunction from %s (pass-through until the observe/swallow flags)", where);
    } else {
      snprintf(line, sizeof line, "DockRemote B1.16.8: NOT hooked (MSHookFunction %s, GSSendEvent %s): safe no-op",
               hook ? "found" : "not exported by any substrate library", target ? "found" : "missing");
    }
    syslog(LOG_NOTICE, "%s", line);
    logLine(line);  // one line per iapd start, flags or not, so the hook's state is visible without syslog
  } @catch (NSException* e) {}
}
