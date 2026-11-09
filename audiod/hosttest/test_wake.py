#!/usr/bin/env python3
"""Host test of audiod's wake-on-play trigger (B1.9 B): the display wake fires exactly once per
'none -> source' takeover, never on a source -> source switch or a same-name reconnect, and at most
once per debounce interval. The host build uses the mock waker; the event log records every decision
(kind "wake": "wake+launch <bundle>" or "debounced (...)"). Usage: test_wake.py PATH_TO_fidelio-audiod
"""
import os, socket, struct, subprocess, sys, threading, time
AUDIOD = sys.argv[1]
TAKEOVER, HOLD, RELEASE, IDLE_STOP, LEAD = 400, 1500, 1000, 2000, 750
TMP = "/tmp/audiod-wake"
os.makedirs(TMP, exist_ok=True)
SOCK, OUT, EVT = f"{TMP}/audiod.sock", f"{TMP}/out.pcm", f"{TMP}/events.log"
for f in (OUT, EVT, SOCK):
    if os.path.exists(f): os.unlink(f)
proc = subprocess.Popen([AUDIOD, "--sock", SOCK, "--pcm-out", OUT, "--event-log", EVT, "--no-status", "--no-silence",
                         "--takeover-ms", str(TAKEOVER), "--hold-ms", str(HOLD), "--release-ms", str(RELEASE),
                         "--idle-stop-ms", str(IDLE_STOP), "--lead-ms", str(LEAD), "--wake-on-play", "on", "--wake-bundle", "com.fidelio.nowplaying"],
                        stdout=open(f"{TMP}/audiod.log", "w"), stderr=subprocess.STDOUT)
for _ in range(50):
    if os.path.exists(SOCK): break
    time.sleep(0.05)
fails = []
def check(ok, msg):
    print(("ok   " if ok else "FAIL ") + msg)
    if not ok: fails.append(msg)
def hello(name): return struct.pack("<BBBBI32s", 1, 0, 2, 16, 44100, name.encode())
class Client:
    def __init__(self, name, sample):
        self.s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM); self.s.connect(SOCK)
        self.s.sendall(struct.pack("<II", 1, 40) + hello(name))
        self.sample, self.stop, self.t = sample, False, None
    def play(self, ms):
        """stream `ms` of audio in real time (plus the LEAD burst), then return (the source goes quiet)"""
        def run():
            chunk = struct.pack("<hh", self.sample, self.sample) * 882
            n = (ms + LEAD) // 20
            t0 = time.monotonic()
            for i in range(n):
                if self.stop: break
                d = (i * 20 - LEAD) / 1000 - (time.monotonic() - t0)
                if d > 0: time.sleep(d)
                try: self.s.sendall(struct.pack("<II", 2, len(chunk)) + chunk)
                except OSError: break
        self.t = threading.Thread(target=run, daemon=True); self.t.start()
    def wait(self):
        if self.t: self.t.join()
    def close(self):
        self.stop = True; self.wait(); self.s.close()
def events(kind=None):
    ev = []
    for line in open(EVT):
        t, k, frm, to, *reason = line.split()
        if kind is None or k == kind: ev.append((int(t), k, frm, to, " ".join(reason)))
    return ev

T0 = time.monotonic()
def until(ms):
    d = ms / 1000 - (time.monotonic() - T0)
    if d > 0: time.sleep(d)
def now(): return (time.monotonic() - T0) * 1000
# 1. none -> A: one wake at the takeover (~0.45 s)
a = Client("spotify", 0x1111); a.play(1200)
until(1000)
w = events("wake")
check(len(w) == 1 and w[0][3] == "spotify" and w[0][4].startswith("wake+launch com.fidelio.nowplaying"), f"none -> spotify: one wake, launching the app ({w})")
wake1 = w[0][0] if w else 0
# 2. B steals once A's hold expires (~1.95 s): no new wake
b = Client("airplay", 0x2222); b.play(1500)
until(2600)
tk = events("takeover")
check(len(tk) >= 2 and tk[1][2] == "spotify" and tk[1][3] == "airplay", f"airplay took over from spotify ({tk[1:2]})")
check(len(events("wake")) == 1, f"a source -> source switch does not wake ({len(events('wake'))} wake events)")
# 3. B's bytes stop at 2.3 s -> quiet release at ~3.3 s; C starts at 3.5 s, inside the 5 s window after wake #1: debounced
until(3500)
rl = events("release")
check(any(r[2] == "airplay" for r in rl), f"airplay released ({rl})")
c = Client("quiet", 0x3333); c.play(1000)
until(4400)
w = events("wake")
check(len(w) == 2 and w[1][3] == "quiet" and w[1][4].startswith("debounced"), f"a second none -> source {w[1][0] - wake1 if len(w) > 1 else -1} ms after the first is debounced ({w[1:]})")
# 4. C released at ~5.5 s; D starts at 5.7 s, more than 5 s after wake #1: wakes again
until(5700)
d = Client("spotify2", 0x4444); d.play(1000)
until(6600)
w = events("wake")
check(len(w) == 3 and w[2][3] == "spotify2" and w[2][4].startswith("wake+launch"), f"none -> source {w[2][0] - wake1 if len(w) > 2 else -1} ms after the first wake (> 5000) wakes again ({w[2:]})")
# 5. a fresh source 'raop' starts after the window (wake #4); it drops its connection mid-stream and comes back
#    at once (same name, within the ghost window): a 'reconnect' takeover, which must NOT wake
until(11700)
e = Client("raop", 0x2222); e.play(3000)
until(12900)
e.close()
e2 = Client("raop", 0x2222); e2.play(1500)
until(14800)
tk = events("takeover")
check(any(t[4] == "reconnect" and t[2] == "raop" for t in tk), f"the reconnect was a 'reconnect' takeover ({[t for t in tk if t[4] == 'reconnect']})")
w = events("wake")
check(len(w) == 4 and w[3][3] == "raop" and w[3][4].startswith("wake+launch"), f"raop's first start woke (event #4) and the reconnect added none ({len(w)} wake events)")
a.close(); b.close(); c.close(); d.close(); e2.close()
proc.terminate(); proc.wait()
print("\nevents:")
for ev in events(): print(f"  {ev[0]:6d} {ev[1]:10s} {ev[2]:>8s} -> {ev[3]:<8s} {ev[4]}")
print("\n" + (f"{len(fails)} FAILURES" if fails else "all wake-on-play checks passed"))
sys.exit(1 if fails else 0)
