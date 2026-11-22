#pragma once
// B1.16.13: the dock-key decode of the DockRemote tweak, its swallow decisions and the readable key log.
// Plain C++ with no Objective-C and no fixed paths, so the host test (hosttest/keys_test.cpp) runs the
// same code the tweak runs inside iapd.
//
// The type-4001 record as measured on the phone (iPhone OS 3.1.3, 2026-11-22, DockRemote 4ed6a5a observing,
// two key passes compared with iapd's own log of the same presses):
//   - header 52 bytes: type @0 (4001), subtype @4 (0), flags @40, infoSize @48 (8); info 8 bytes @52.
//   - the 32-bit word @56 is a COMMAND code; the word @52 was 0 in every event.
//   - flags 0x400 = the first event of a press (down), 0x800 = the second (up); both carry the same code.
//   - play/pause, menu, select, up: two events (0x400, then 0x800). next and previous (taps): ONE event,
//     flags 0x800. A type-101 event (flags 0, info all zero) precedes every press and every dock volume step.
// The B1.16.8 layout (key as uint16 @52, state 0/1 @56) was wrong.
#include <stddef.h>
#include <stdint.h>

namespace dockremote {

// GSEventRecord byte offsets (iPhone OS 3.1).
enum { GS_OFF_TYPE = 0, GS_OFF_SUBTYPE = 4, GS_OFF_FLAGS = 40, GS_OFF_INFOSIZE = 48, GS_HEADER = 52,
       GS_OFF_INFO0 = 52, GS_OFF_CODE = 56 };
enum { GS_TYPE_ACCESSORY_KEY = 4001, GS_KEY_INFO_SIZE = 8 };
enum { KEY_FLAG_DOWN = 0x400, KEY_FLAG_UP = 0x800 };

// The command codes of the word @56.
enum {
  CODE_PLAYPAUSE    = 0,   // two events (0x400, 0x800)
  CODE_SEEK_FORWARD = 3,   // UNVERIFIED: iapd's own key number for next; read as a held next (fast-forward)
  CODE_REWIND       = 4,   // UNVERIFIED: seen once, as one 0x800 event, when a previous press reached iapd twice
  CODE_NEXT         = 20,  // a tap: one event, 0x800
  CODE_PREVIOUS     = 21,  // a tap: one event, 0x800
  CODE_MENU         = 22,  // two events; = iapd's key 22
  CODE_SELECT       = 23,  // two events; = iapd's key 23
  CODE_UP           = 24,  // two events; = iapd's key 24
  CODE_DOWN         = 25,  // UNVERIFIED: iapd's key 25, extrapolated; down never arrived in the observe runs
};

struct KeyEvent {
  bool valid;     // a type-4001 record of the measured shape; anything else is not a key and passes through
  bool down;      // flags 0x400 (false: 0x800)
  uint32_t code;  // the word @56
};

// Decode a GSEventRecord. Valid only when type == 4001, infoSize == 8, the word @52 is 0 and the flags
// word is exactly 0x400 or 0x800. The record must be readable for 60 bytes when type and infoSize match
// (the header is 52 bytes and infoSize says 8 more follow); fields are read with memcpy.
KeyEvent decodeKey(const void* record);

// The audiod transport verb a code is taken over for (play/pause TOGGLE, next NEXT, previous PREV), or 0:
// every other code always passes through.
uint8_t verbForCode(uint32_t code);

// The key's log name, without spaces: play-pause, next, previous, menu, select, up, down, seek-forward,
// rewind, or code-N (written into buf) for an unknown code.
const char* keyName(uint32_t code, char* buf, size_t cap);

// What the hook does with one key event.
struct Decision {
  bool swallow;   // drop the record (SpringBoard and the iPod app never see it)
  uint8_t verb;   // non-zero: send this verb to audiod now (at most once per press)
  bool logLine;   // the press is complete (this is its 0x800 event): write its line to remote-keys.log
  bool taken;     // the press was taken over (for the log line: taken|passed)
};

// "May this press be taken over?" Asked once per press, at its first event, and only for a code with a
// verb. The hook answers from the swallow flag and DockRemoteCore's shouldForward().
typedef bool (*MayTakeFn)(uint32_t code, void* ctx);

// Follows the presses. A press is an optional 0x400 event and a 0x800 event of the same code. The
// decision is made at the press's first event and applies to all its events, so a press is either taken
// whole or passed whole, and the verb goes out once, at the first event:
//   play/pause: 0x400 decides and sends TOGGLE, its 0x800 follows that decision.
//   next / previous: the single 0x800 event decides and sends NEXT / PREV.
//   any other code: always passed.
// A 0x400 replaces any press still pending; a 0x800 of another code is a press of its own and leaves the
// pending one alone.
class PressTracker {
 public:
  PressTracker() : pending_(false), pendingCode_(0), pendingTaken_(false) {}
  Decision onKey(const KeyEvent& e, MayTakeFn mayTake, void* ctx);
  bool pendingTaken() const { return pending_ && pendingTaken_; }  // a taken press is waiting for its 0x800
 private:
  bool pending_;
  uint32_t pendingCode_;
  bool pendingTaken_;
};

// remote-keys.log: one line per completed press, "<unix seconds> <code> <name> <taken|passed>".
enum { KEY_LOG_CAP = 16 * 1024 };  // above this size the file is started afresh before the next line
int formatKeyLine(char* out, size_t cap, long long unixSeconds, uint32_t code, bool taken);
// Append line plus '\n' with one open/write/close (mode 0644 when created). When the file is larger than
// capBytes it is truncated first. Returns true when the whole line was written.
bool appendKeyLine(const char* path, const char* line, size_t capBytes);

}  // namespace dockremote
