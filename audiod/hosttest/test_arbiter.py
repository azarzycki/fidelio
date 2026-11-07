#!/usr/bin/env python3
"""Host test for fidelio-audiod's arbitration (audiod/DESIGN.md).

Starts audiod with --pcm-out/--event-log, opens two client connections ("spotify" = A sends sample
0x1111, "airplay" = B sends 0x2222), streams timed PCM bursts (20 ms chunks, real time) and asserts
that the active-source transitions in the event log land at the TAKEOVER / HOLD / RELEASE
boundaries, that a rapid-flapping rival never steals, and that the played PCM is exactly the
active sources' audio. Usage: test_arbiter.py PATH_TO_fidelio-audiod
"""
import os, socket, struct, subprocess, sys, threading, time

AUDIOD = sys.argv[1] if len(sys.argv) > 1 else "build/fidelio-audiod"
GAP, TAKEOVER, HOLD, RELEASE, IDLE_STOP = 120, 400, 1500, 1000, 2000  # idle-stop shortened for the test
TOL = 120  # ms tolerance on boundaries (20 ms tick + socket + python jitter)
TMP = "/tmp/audiod-test"
os.makedirs(TMP, exist_ok=True)
SOCK = f"{TMP}/audiod.sock"; OUT = f"{TMP}/out.pcm"; EVT = f"{TMP}/events.log"; STATUS = TMP
for f in (OUT, EVT, SOCK):
    if os.path.exists(f): os.unlink(f)  # a stale SOCK would make the connect below race the bind

proc = subprocess.Popen([AUDIOD, "--sock", SOCK, "--pcm-out", OUT, "--event-log", EVT, "--status-dir", STATUS, "--no-silence",
                         "--producing-gap-ms", str(GAP), "--takeover-ms", str(TAKEOVER), "--hold-ms", str(HOLD),
                         "--release-ms", str(RELEASE), "--idle-stop-ms", str(IDLE_STOP)],
                        stdout=open(f"{TMP}/audiod.log", "w"), stderr=subprocess.STDOUT)
for _ in range(50):
    if os.path.exists(SOCK): break
    time.sleep(0.05)

T0 = time.monotonic()
def ms(): return int((time.monotonic() - T0) * 1000)

def hello(name, prio=0):
    return struct.pack("<BBBBI32s", 1, prio, 2, 16, 44100, name.encode())

class Client:
    def __init__(self, name, sample):
        self.name, self.sample = name, sample
        self.s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        for attempt in range(50):
            try: self.s.connect(SOCK); break
            except (ConnectionRefusedError, FileNotFoundError):
                if attempt == 49: raise
                time.sleep(0.05)
        self.send(1, hello(name))
        self.chunk = struct.pack("<hh", sample, sample) * 882  # 20 ms
        self.thread = None; self.schedule = []  # list of (start_ms, end_ms) producing windows
    def send(self, t, payload=b""):
        self.s.sendall(struct.pack("<II", t, len(payload)) + payload)
    def stream(self, windows):
        """windows: list of (start_ms, end_ms) relative to T0, during which AUDIO is sent every 20 ms."""
        def run():
            for (a, b) in windows:
                i = 0
                while True:
                    at = a + i * 20
                    if at >= b: break
                    d = at - ms()
                    if d > 0: time.sleep(d / 1000)
                    self.send(2, self.chunk); i += 1
        self.thread = threading.Thread(target=run, daemon=True); self.thread.start()
    def join(self): self.thread.join()
    def flush(self): self.send(3)
    def stop(self): self.send(4)
    def close(self): self.s.close()

A = Client("spotify", 0x1111)
B = Client("airplay", 0x2222)

# Scenario (ms):
#   A streams 0..4000 and 4000..9000 continuously (one window). B streams 1000..4000 (sustained),
#   then flaps 6000..7800 (200 ms on / 300 ms off), then streams 8000..9000 sustained, then nothing.
#   Expected: A active at 400 (takeover from none); B steals at 1900 (A's hold expired, B producing
#   since 1000 >= 400); A does NOT steal back while B keeps streaming (A is the older stream);
#   B quiet from 4000 -> released at 5000, A takes over at 5000 (already producing >= 400);
#   B's flaps never sustain 400 ms -> no transition during 6000..7800; B sustained from 8000 ->
#   steals at 8400 (A's hold 5000+1500 already expired); both stop at 9000 -> B released at 10000;
#   idle-stop at 12000 (IDLE_STOP=2000 here).
A.stream([(0, 9000)])
flaps = [(6000 + k * 500, 6000 + k * 500 + 200) for k in range(4)]  # 6000-6200, 6500-6700, 7000-7200, 7500-7700
B.stream([(1000, 4000)] + flaps + [(8000, 9000)])
A.join(); B.join()
time.sleep((IDLE_STOP + RELEASE + 800) / 1000)
A.close(); B.close()
time.sleep(0.3)
proc.terminate(); proc.wait()

events = []
for line in open(EVT):
    t, kind, frm, to, *reason = line.split()
    events.append((int(t), kind, frm, to, " ".join(reason)))
print("events:")
for e in events: print(f"  {e[0]:6d} {e[1]:12s} {e[2]:>8s} -> {e[3]:<8s} {e[4]}")

fails = []
def expect(idx, kind, frm, to, at, tol=TOL):
    ok = idx < len(events) and events[idx][1] == kind and events[idx][2] == frm and events[idx][3] == to and abs(events[idx][0] - at) <= tol
    got = f"{events[idx][0]} {events[idx][1]} {events[idx][2]}->{events[idx][3]}" if idx < len(events) else "nothing"
    print(("ok   " if ok else "FAIL ") + f"event {idx}: {kind} {frm}->{to} at {at}±{tol} (got {got})")
    if not ok: fails.append(idx)

# event log starts with output-start then the first takeover
transitions = [e for e in events if e[1] in ("takeover", "release", "idle-stop")]
events = transitions
expect(0, "takeover", "none", "spotify", TAKEOVER)                 # A after 400 ms producing
expect(1, "takeover", "spotify", "airplay", TAKEOVER + HOLD)       # B at 1900: A held 1500 since 400
expect(2, "release", "airplay", "none", 4000 + RELEASE)            # B quiet 1000 ms after stopping at 4000
expect(3, "takeover", "none", "spotify", 4000 + RELEASE)           # A takes over right away (producing >= 400)
expect(4, "takeover", "spotify", "airplay", 8000 + TAKEOVER)       # B sustained from 8000; no flap stole before
expect(5, "release", "airplay", "none", 9000 + RELEASE)
expect(6, "idle-stop", "none", "none", 9000 + RELEASE + IDLE_STOP)
extra = len(events) - 7
print(("ok   " if extra == 0 else "FAIL ") + f"no extra transitions (flaps 6000..7800 never stole): {extra} extra")
if extra: fails.append("extra")
# hold: no transition between 1900 and 3400 while both streamed; most-recent: A never stole back from B
between = [e for e in events if 1900 + TOL < e[0] < 4000]
print(("ok   " if not between else "FAIL ") + f"no transition while both streamed (1900..4000): {between}")
if between: fails.append("hold")

# PCM: the output must be A-samples for ~0.4..1.9 s, B for ~1.9..4.0, A for ~5.0..8.4, B for ~8.4..9.0.
data = open(OUT, "rb").read()
frames = len(data) // 4
a_frames = data.count(struct.pack("<hh", 0x1111, 0x1111)); b_frames = data.count(struct.pack("<hh", 0x2222, 0x2222))
exp_a = (1900 - 400 + 8400 - 5000) * 44.1; exp_b = (4000 - 1900 + 9000 - 8400) * 44.1
ok = abs(a_frames - exp_a) < 0.15 * exp_a and abs(b_frames - exp_b) < 0.15 * exp_b and a_frames + b_frames >= frames * 0.98
print(("ok   " if ok else "FAIL ") + f"played PCM: A {a_frames} frames (expect ~{int(exp_a)}), B {b_frames} (expect ~{int(exp_b)}), total {frames}")
if not ok: fails.append("pcm")
act = open(f"{STATUS}/active-source").read().strip()
print(("ok   " if act == "none" else "FAIL ") + f"active-source file ends as 'none' (got '{act}')")
if act != "none": fails.append("active-source")
print("\n" + (f"{len(fails)} FAILURES" if fails else "all arbitration checks passed"))
sys.exit(1 if fails else 0)
