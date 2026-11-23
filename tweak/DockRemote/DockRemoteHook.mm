// DockRemote, the MobileSubstrate hook inside iapd (iPhone OS 3.1.3 phone only).
//
// B1.16.8: the hook point is GraphicsServices' GSSendEvent, not -[SimpleRemote sendEventRecord:toApp:].
// Device findings (cspot-build-local):
//  - b051bfc bound to that method, but remote-events.log stayed empty for all seven keys, asleep and
//    awake: iapd's C++ SimpleRemoteEventHandler never calls it for keys.
//  - iapd imports _GSSendEvent and _GSSendSystemEvent, but not GSEventAccessoryKeyStateChanged.
//  - In the 3.1.2 SDK's GraphicsServices (armv6, disassembled), an accessory key is a 60-byte
//    GSEventRecord of type 4001 (0x0FA1): flags @40, infoSize @48 = 8, 8 bytes of info @52.
//    It is sent with GSSendSystemEvent, which is GSSendEvent(record, the purple system event port).
//  So every key iapd hands to SpringBoard passes GSSendEvent, which MSHookFunction patches in place:
//  calls from inside GraphicsServices are seen too.
// B1.16.13: the decode is the one measured on the phone (DockRemoteKeys.h): the word @56 is a command
// code, the flags word says down (0x400) or up (0x800), next and previous arrive as one 0x800 event. The
// B1.16.8 reading (key @52, state 0/1 @56) was wrong.
// B1.16.15: codes 4 (measured for the left key) and 3 (assumed for the right key) are previous and next
// as well (DockRemoteKeys.h).
// B1.16.16: the take-or-pass condition follows audiod's remote target (remote-target), so a source of
// ours that the remote paused keeps the keys and play/pause resumes it (DockRemoteCore.h).
// Not used on the 5.1.1 iPad: its iapd imports no GSSendEvent and routes the keys with
// MRMediaRemoteSendCommand to the now-playing app, so that is an app-level route (NowPlaying), not a tweak.
//
// STAGING:
//  - With no flags the hook is a pure pass-through that writes nothing.
//  - /var/mobile/fidelio/remote-tweak.observe: log every record iapd sends to remote-events.log (still
//    passed through).
//  - /var/mobile/fidelio/remote-tweak.observe OR remote-tweak.swallow: one line per completed key press
//    in remote-keys.log ("<unix seconds> <code> <name> <taken|passed>"), read by NowPlaying's Technical tab.
//  - /var/mobile/fidelio/remote-tweak.swallow: take over play/pause (0), next (20, 3) and previous (21, 4):
//    the verb goes to audiod once, at the press's first event, and every event of the press is dropped.
//    Only when DockRemoteCore's shouldForward() agrees (setting on, a source of ours is audiod's remote
//    target, audiod reachable); it is asked once per press. Everything else always passes through.
//  - /var/mobile/fidelio/remote-tweak.disabled: a complete no-op, checked on every record.
//  A crash in iapd breaks the dock until a reseat. Fields are therefore read with memcpy at fixed
//  offsets, the info dump is bounded, and nothing blocks beyond DockRemoteCore's short socket timeouts.
#import <Foundation/Foundation.h>
#import <dlfcn.h>
#import <mach/mach.h>
#import <pthread.h>
#import <string.h>
#import <sys/stat.h>
#import <syslog.h>
#import <time.h>

#import "DockRemoteCore.h"
#import "DockRemoteKeys.h"

static const char* kObserve = "/var/mobile/fidelio/remote-tweak.observe";
static const char* kSwallow = "/var/mobile/fidelio/remote-tweak.swallow";
static const char* kDisable = "/var/mobile/fidelio/remote-tweak.disabled";
static const char* kLog     = "/var/mobile/fidelio/remote-events.log";
static const char* kKeys    = "/var/mobile/fidelio/remote-keys.log";  // B1.16.13: for NowPlaying

using namespace dockremote;

typedef int (*GSSendEvent_t)(const void* record, mach_port_t port);
typedef void (*MSHookFunction_t)(void* symbol, void* replace, void** result);
static GSSendEvent_t orig_GSSendEvent = 0;

static PressTracker gPresses;                                      // guarded by gPressLock
static pthread_mutex_t gPressLock = PTHREAD_MUTEX_INITIALIZER;

static bool exists(const char* p) { struct stat st; return stat(p, &st) == 0; }
static uint32_t rd32(const unsigned char* p, unsigned off) { uint32_t v; memcpy(&v, p + off, 4); return v; }

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

// Asked once per press, at its first event, for a code with a verb (PressTracker).
static bool mayTake(uint32_t code, void* swallowFlag) { return *(bool*)swallowFlag && shouldForward(code); }

static int new_GSSendEvent(const void* record, mach_port_t port) {
  @try {
    if (record && !exists(kDisable)) {
      const unsigned char* p = (const unsigned char*)record;
      bool observe = exists(kObserve);
      if (observe) {
        // The evidence format of B1.16.8, unchanged apart from the KEY part's wording.
        int type = (int)rd32(p, GS_OFF_TYPE);
        unsigned subtype = rd32(p, GS_OFF_SUBTYPE), flags = rd32(p, GS_OFF_FLAGS), infoSize = rd32(p, GS_OFF_INFOSIZE);
        bool keyShape = type == GS_TYPE_ACCESSORY_KEY && infoSize == GS_KEY_INFO_SIZE;  // 60 bytes readable
        char h[200], in[120], line[460];
        unsigned n = infoSize <= 32 ? infoSize : 32;  // a sane info size: dump at most 32 bytes of it
        hex(h, sizeof h, p, GS_HEADER);
        hex(in, sizeof in, p + GS_HEADER, infoSize <= 4096 ? n : 0);
        if (keyShape) snprintf(line, sizeof line, "gs type=%d subtype=%u flags=0x%x infoSize=%u port=%u KEY code=%u flags=0x%x hdr=[%s] info=[%s]",
                               type, subtype, flags, infoSize, (unsigned)port, rd32(p, GS_OFF_CODE), flags, h, in);
        else snprintf(line, sizeof line, "gs type=%d subtype=%u flags=0x%x infoSize=%u port=%u hdr=[%s] info=[%s]", type, subtype, flags, infoSize,
                      (unsigned)port, h, in);
        logLine(line);
      }
      KeyEvent k = decodeKey(record);
      if (k.valid) {
        bool swallow = exists(kSwallow);
        Decision d = { false, 0, false, false };
        bool track;
        pthread_mutex_lock(&gPressLock);
        // With neither flag the hook writes nothing and passes everything, as before; but a press that was
        // taken at its 0x400 event keeps being followed, so its 0x800 never reaches the iPod app alone.
        track = observe || swallow || gPresses.pendingTaken();
        if (track) d = gPresses.onKey(k, mayTake, &swallow);
        pthread_mutex_unlock(&gPressLock);
        if (track) {
          if (d.verb) {
            bool sent = forward(d.verb);
            if (observe) {
              char line[120];
              snprintf(line, sizeof line, "TAKEN code=%u flags=0x%x -> audiod verb=%u (%s)", (unsigned)k.code,
                       k.down ? KEY_FLAG_DOWN : KEY_FLAG_UP, d.verb, sent ? "sent" : "send failed");
              logLine(line);
            }
          }
          if (d.logLine) {
            char line[96];
            formatKeyLine(line, sizeof line, (long long)time(0), k.code, d.taken);
            appendKeyLine(kKeys, line, KEY_LOG_CAP);
          }
          if (d.swallow) return KERN_SUCCESS;  // dropped: SpringBoard (and the iPod app) never see this event
        }
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
      snprintf(line, sizeof line, "DockRemote B1.16.16: hooked GSSendEvent with MSHookFunction from %s (pass-through until the observe/swallow flags)", where);
    } else {
      snprintf(line, sizeof line, "DockRemote B1.16.16: NOT hooked (MSHookFunction %s, GSSendEvent %s): safe no-op",
               hook ? "found" : "not exported by any substrate library", target ? "found" : "missing");
    }
    syslog(LOG_NOTICE, "%s", line);
    logLine(line);  // one line per iapd start, flags or not, so the hook's state is visible without syslog
  } @catch (NSException* e) {}
}
