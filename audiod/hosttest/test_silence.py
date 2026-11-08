#!/usr/bin/env python3
"""Host test for fidelio-audiod's silence detection and stream recency (B1.2 review items #3, #6).

  test_silence.py PATH_TO_fidelio-audiod

Scenario 1 (quiet music is not a pause): one source streams a realistic envelope through audiod:
2 s loud, a 3 s fade down to -40 dBFS, 4 s of pianissimo (peak 60), a 2 s exact-zero inter-track gap,
2 s loud again, then 12 s of exact zeros (a paused sender). audiod must take over once and release
ONCE, as a "silent" release QUIET_RELEASE after the last loud frame, never in between, and the
played PCM must be one contiguous, byte-exact slice of what was sent (no flush ate a fade).

Scenario 2 (a background hiccup is not a user action): X and Y stream loud, Y older. Y stalls for
1.2 s (< RELEASE, a wifi hiccup) and later for 2 s (>= RELEASE). The first stall must not let Y steal
the speaker from X; the second makes Y a new stream, which may.

Scenario 3 (B1.3 #2, reconnect is the same stream): X older, Y newer and active. Y's connection
drops and a new connection with the same name comes back 0.5 s later and keeps streaming: audiod
must keep Y active (a "reconnect" takeover, no release, X never active), i.e. the reconnect
inherited Y's recency and its active slot.

Scenario 4 (B1.3 #3, resume after zeros): X older loud; Y newer loud, then exact zeros for
QUIET_RELEASE (released "silent", X takes over), then loud again: the user pressed play, so Y must
take the speaker back once X's hold expires.
"""
import os, socket, struct, subprocess, sys, threading, time

AUDIOD = sys.argv[1] if len(sys.argv) > 1 else "build/fidelio-audiod"
TAKEOVER, HOLD, RELEASE, QUIET_RELEASE, IDLE_STOP, THRESHOLD, LEAD = 400, 1500, 1500, 3000, 2000, 24, 750
TOL = 150
TMP = "/tmp/audiod-silence"
os.makedirs(TMP, exist_ok=True)
fails = []

def check(ok, msg):
    print(("ok   " if ok else "FAIL ") + msg)
    if not ok: fails.append(msg)

def start_audiod(tag):
    sock, out, evt = f"{TMP}/{tag}.sock", f"{TMP}/{tag}.pcm", f"{TMP}/{tag}.events"
    for f in (sock, out, evt):
        if os.path.exists(f): os.unlink(f)
    p = subprocess.Popen([AUDIOD, "--sock", sock, "--pcm-out", out, "--event-log", evt, "--no-status", "--no-silence",
                          "--takeover-ms", str(TAKEOVER), "--hold-ms", str(HOLD), "--release-ms", str(RELEASE),
                          "--quiet-release-ms", str(QUIET_RELEASE), "--idle-stop-ms", str(IDLE_STOP),
                          "--silence-threshold", str(THRESHOLD), "--lead-ms", str(LEAD)],
                         stdout=open(f"{TMP}/{tag}.log", "w"), stderr=subprocess.STDOUT)
    for _ in range(50):
        if os.path.exists(sock): break
        time.sleep(0.05)
    return p, sock, out, evt

def events(evt):
    ev = []
    for line in open(evt):
        t, kind, frm, to, *reason = line.split()
        if kind in ("takeover", "release", "idle-stop"): ev.append((int(t), kind, frm, to, " ".join(reason)))
    return ev

class Client:
    """Sends 20 ms chunks LEAD ms ahead of real time like SocketSink; chunk(i) gives the PCM for chunk i."""
    def __init__(self, sock, name, chunk):
        self.chunk = chunk
        self.s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        for attempt in range(50):
            try: self.s.connect(sock); break
            except (ConnectionRefusedError, FileNotFoundError):
                if attempt == 49: raise
                time.sleep(0.05)
        self.send(1, struct.pack("<BBBBI32s", 1, 0, 2, 16, 44100, name.encode()))
        self.sent = bytearray()
    def send(self, t, payload=b""): self.s.sendall(struct.pack("<II", t, len(payload)) + payload)
    def stream(self, t0, windows):
        """windows: (start_ms, end_ms): chunks whose timeline t is in [start, end) go out at wall max(start, t-LEAD)."""
        def run():
            for a, b in windows:
                i = a // 20
                while i * 20 < b:
                    t = i * 20
                    d = max(a, t - LEAD) / 1000 - (time.monotonic() - t0)
                    if d > 0: time.sleep(d)
                    data = self.chunk(i)
                    self.sent += data
                    self.send(2, data); i += 1
        self.th = threading.Thread(target=run, daemon=True); self.th.start()
    def join(self): self.th.join()
    def close(self): self.s.close()

def prng(i):  # deterministic pseudo-random sample in -1..1 per frame index
    x = (i * 2654435761 + 12345) & 0xFFFFFFFF
    return ((x >> 8) & 0xFFFF) / 32768.0 - 1.0

# ------------------------------------------------------------------ scenario 1
print("== scenario 1: quiet music through audiod")
p, sock, out, evt = start_audiod("quiet")
def envelope_amp(ms):
    if ms < 2000: return 8000                                   # loud
    if ms < 5000: return 8000 * (0.01 ** ((ms - 2000) / 3000))  # fade to -40 dBFS (80) over 3 s
    if ms < 9000: return 60                                     # pianissimo, above the 24 threshold
    if ms < 11000: return 0                                     # exact zeros: an inter-track gap
    if ms < 13000: return 8000                                  # loud again
    return 0                                                    # paused sender: zeros from 13 s on
def chunk1(i):
    amp = envelope_amp(i * 20)
    return b"".join(struct.pack("<hh", int(amp * prng(i * 882 + k)), int(amp * prng(i * 882 + k + 7))) for k in range(882))
c = Client(sock, "airplay", chunk1)
t0 = time.monotonic()
c.stream(t0, [(0, 13000 + 12000)])
c.join()
time.sleep((QUIET_RELEASE + 1000) / 1000)
c.close(); time.sleep(0.3); p.terminate(); p.wait()
ev = events(evt)
for e in ev: print(f"   {e[0]:6d} {e[1]:9s} {e[2]:>8s} -> {e[3]:<8s} {e[4]}")
tk = [e for e in ev if e[1] == "takeover"]; rl = [e for e in ev if e[1] == "release"]
check(len(tk) == 1 and abs(tk[0][0] - TAKEOVER) <= TOL, f"one takeover at ~{TAKEOVER} ms ({tk})")
check(len(rl) == 1 and rl[0][4].startswith("silent") and abs(rl[0][0] - (13000 + QUIET_RELEASE)) <= TOL + LEAD,
      f"one release, 'silent', ~{QUIET_RELEASE} ms after the last loud frame (the fade / pianissimo / 2 s gap did not release) ({rl})")
data = open(out, "rb").read(); sent = bytes(c.sent)
start = sent.find(data[:64]) if len(data) >= 64 else -1
n = 0
if start >= 0:
    while n < len(data) and start + n < len(sent) and data[n] == sent[start + n]: n += 1
check(start >= 0 and n == len(data) and n >= (13000 - TAKEOVER - 200) * 44.1 * 4,
      f"played PCM is one contiguous byte-exact slice of the stream: starts at {start / 4 / 44.1:.0f} ms, {n / 4 / 44.1:.0f} ms long (no flush inside)")

# ------------------------------------------------------------------ scenario 2
print("== scenario 2: a background hiccup must not steal")
p, sock, out, evt = start_audiod("hiccup")
def loud(tag):
    return lambda i: b"".join(struct.pack("<hh", int(8000 * prng(i * 882 + k + tag)), int(8000 * prng(i * 882 + k + tag + 3))) for k in range(882))
Y = Client(sock, "spotify", loud(1))   # older stream
X = Client(sock, "airplay", loud(2))   # newer stream
t0 = time.monotonic()
# Windows are timeline positions sent LEAD ahead: the last chunk of a window ending at E goes out at
# wall E - LEAD, the first chunk of the next window at its start. So the gap audiod sees in BYTES is
# (next start) - (previous end - LEAD): 5450 - 4250 = 1.2 s (< RELEASE), 9250 - 7250 = 2.0 s (>= RELEASE).
HICCUP_END, GAP_END = 5000 - LEAD + 1200, 8000 - LEAD + 2000
END = GAP_END + 2000
Y.stream(t0, [(0, 5000), (HICCUP_END, 8000), (GAP_END, END)])
X.stream(t0, [(1000, END)])
Y.join(); X.join()
time.sleep((RELEASE + IDLE_STOP + 800) / 1000)
Y.close(); X.close(); time.sleep(0.3); p.terminate(); p.wait()
ev = events(evt)
for e in ev: print(f"   {e[0]:6d} {e[1]:9s} {e[2]:>8s} -> {e[3]:<8s} {e[4]}")
def ev_at(i, kind, frm, to, at):
    ok = i < len(ev) and ev[i][1] == kind and ev[i][2] == frm and ev[i][3] == to and abs(ev[i][0] - at) <= TOL
    check(ok, f"event {i}: {kind} {frm}->{to} at ~{at} (got {ev[i][:4] if i < len(ev) else 'nothing'})")
ev_at(0, "takeover", "none", "spotify", TAKEOVER)                 # Y first
ev_at(1, "takeover", "spotify", "airplay", TAKEOVER + HOLD)       # X newer, steals when Y's hold expires
# Y's 1.2 s hiccup (5000..6200): its stream is still the older one -> no steal
between = [e for e in ev if TAKEOVER + HOLD + TOL < e[0] < GAP_END + TAKEOVER - TOL]
check(not between, f"no transition between 1.9 s and {(GAP_END + TAKEOVER) / 1000:.1f} s: Y's 1.2 s byte gap did not make it 'newer' ({between})")
ev_at(2, "takeover", "airplay", "spotify", GAP_END + TAKEOVER)    # after a >= RELEASE byte gap Y is a new stream: may steal
ev_at(3, "release", "spotify", "none", END - LEAD + RELEASE)              # the last bytes went out LEAD early
ev_at(4, "idle-stop", "none", "none", END - LEAD + RELEASE + IDLE_STOP)
check(len(ev) == 5, f"exactly 5 transitions ({len(ev)})")

# ------------------------------------------------------------------ scenario 3
print("== scenario 3: a same-name reconnect within 10 s is the same stream")
p, sock, out, evt = start_audiod("reconnect")
X = Client(sock, "spotify", loud(3))   # older
Y = Client(sock, "airplay", loud(4))   # newer, active from 1900
t0 = time.monotonic()
X.stream(t0, [(0, 9000)])
Y.stream(t0, [(1000, 5000)])
Y.join()
Y.close()                               # Y's connection drops at ~4250 wall (its lead was sent ahead)
time.sleep(0.5)
Y2 = Client(sock, "airplay", loud(4))   # same name, new connection, continues the stream
Y2.stream(t0, [(5000, 9000)])
X.join(); Y2.join()
time.sleep((RELEASE + IDLE_STOP + 800) / 1000)
X.close(); Y2.close(); time.sleep(0.3); p.terminate(); p.wait()
ev = events(evt)
for e in ev: print(f"   {e[0]:6d} {e[1]:9s} {e[2]:>8s} -> {e[3]:<8s} {e[4]}")
ev_at(0, "takeover", "none", "spotify", TAKEOVER)
ev_at(1, "takeover", "spotify", "airplay", TAKEOVER + HOLD)
rc = [e for e in ev if e[1] == "takeover" and e[2] == "airplay" and e[3] == "airplay" and e[4] == "reconnect"]
check(len(rc) == 1, f"the reconnect re-attached to the active airplay stream ({rc})")
bad = [e for e in ev if (e[1] == "release" and e[0] < 9000 - LEAD) or (e[1] == "takeover" and e[3] == "spotify" and e[0] > TAKEOVER + TOL)]
check(not bad, f"no release and no spotify takeover during Y's 0.5 s reconnect ({bad})")
ev_at(len(ev) - 2, "release", "airplay", "none", 9000 - LEAD + RELEASE)
check(len(ev) == 5, f"exactly 5 transitions ({len(ev)})")

# ------------------------------------------------------------------ scenario 4
print("== scenario 4: play after a long silence takes the speaker back")
p, sock, out, evt = start_audiod("resume")
X = Client(sock, "spotify", loud(5))                                   # older, loud throughout
def y_chunk(i):
    t = i * 20
    if 3000 <= t < 3000 + QUIET_RELEASE + 1000: return b"\0" * (882 * 4)  # paused: exact zeros, bytes keep flowing
    return loud(6)(i)
Y = Client(sock, "airplay", y_chunk)                                   # newer
t0 = time.monotonic()
X.stream(t0, [(0, 12000)])
Y.stream(t0, [(1000, 12000)])
X.join(); Y.join()
time.sleep((RELEASE + IDLE_STOP + 800) / 1000)
X.close(); Y.close(); time.sleep(0.3); p.terminate(); p.wait()
ev = events(evt)
for e in ev: print(f"   {e[0]:6d} {e[1]:9s} {e[2]:>8s} -> {e[3]:<8s} {e[4]}")
ev_at(0, "takeover", "none", "spotify", TAKEOVER)
ev_at(1, "takeover", "spotify", "airplay", TAKEOVER + HOLD)
Y_SIL = 3000 - LEAD + QUIET_RELEASE                 # Y's zeros start arriving LEAD early; silent release QUIET_RELEASE after its last loud frame
ev_at(2, "release", "airplay", "none", Y_SIL)
ev_at(3, "takeover", "none", "spotify", Y_SIL)
Y_LOUD = 3000 + QUIET_RELEASE + 1000 - LEAD         # Y's sound resumes (arrives LEAD early)
ev_at(4, "takeover", "spotify", "airplay", max(Y_LOUD + TAKEOVER, Y_SIL + HOLD))
check(len(ev) >= 5 and ev[2][4].startswith("silent"), f"Y's release was a silent release ({ev[2][4] if len(ev) > 2 else 'n/a'})")
print("\n" + (f"{len(fails)} FAILURES" if fails else "all silence/recency checks passed"))
sys.exit(1 if fails else 0)
