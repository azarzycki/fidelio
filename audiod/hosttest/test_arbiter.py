#!/usr/bin/env python3
"""Host test for fidelio-audiod's arbitration (audiod/DESIGN.md).

Starts audiod with --pcm-out/--event-log, opens three client connections ("spotify" = A sends
sample 0x1111, "airplay" = B sends 0x2222, "quiet" = C sends 0x00C8 = 200, below the silence
threshold of 512), streams timed PCM (20 ms chunks) and asserts that the active-source transitions
in the event log land at the TAKEOVER / HOLD / RELEASE boundaries, that a rapid-flapping rival
never steals, that a source streaming digital silence is treated as idle (never steals; an active
source that goes quiet while still sending is released after RELEASE), and that the played PCM is
exactly the active sources' audio.

The clients behave like the real producers behind SocketSink: audio runs LEAD ms ahead of real
time (a LEAD burst at the start of each window, then one 20 ms chunk every 20 ms), so audiod's
per-source ring holds the next LEAD when a takeover happens and every active segment plays its
duration + LEAD of that source's timeline. Usage: test_arbiter.py PATH_TO_fidelio-audiod
"""
import os, socket, struct, subprocess, sys, threading, time

AUDIOD = sys.argv[1] if len(sys.argv) > 1 else "build/fidelio-audiod"
GAP, TAKEOVER, HOLD, RELEASE, IDLE_STOP = 120, 400, 1500, 1500, 2000  # gap short (default is 1000) so the flap test bites; idle-stop short
THRESHOLD = 512
TOL = 120  # ms tolerance on boundaries (20 ms tick + socket + python jitter)
LEAD = 750  # SocketSink::kDefaultLeadMs: how far the producers run ahead of real time
QUIET = 200  # sample value under the threshold
TMP = "/tmp/audiod-test"
os.makedirs(TMP, exist_ok=True)
SOCK = f"{TMP}/audiod.sock"; OUT = f"{TMP}/out.pcm"; EVT = f"{TMP}/events.log"; STATUS = TMP
for f in (OUT, EVT, SOCK):
    if os.path.exists(f): os.unlink(f)  # a stale SOCK would make the connect below race the bind

proc = subprocess.Popen([AUDIOD, "--sock", SOCK, "--pcm-out", OUT, "--event-log", EVT, "--status-dir", STATUS, "--no-silence",
                         "--producing-gap-ms", str(GAP), "--takeover-ms", str(TAKEOVER), "--hold-ms", str(HOLD),
                         "--release-ms", str(RELEASE), "--idle-stop-ms", str(IDLE_STOP), "--silence-threshold", str(THRESHOLD),
                         "--lead-ms", str(LEAD)],
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
        self.thread = None
    def chunk(self, sample): return struct.pack("<hh", sample, sample) * 882  # 20 ms
    def send(self, t, payload=b""):
        self.s.sendall(struct.pack("<II", t, len(payload)) + payload)
    def stream(self, windows):
        """windows: list of (start_ms, end_ms[, sample]) relative to T0: AUDIO is sent every 20 ms during the window,
        carrying the source's timeline a .. b+LEAD (chunk for timeline t goes out at wall time max(a, t-LEAD))."""
        def run():
            for w in windows:
                a, b = w[0], w[1]
                data = self.chunk(w[2] if len(w) > 2 else self.sample)
                i = 0
                while True:
                    t = a + i * 20          # timeline position of this chunk
                    if t >= b + LEAD: break
                    d = max(a, t - LEAD) - ms()
                    if d > 0: time.sleep(d / 1000)
                    self.send(2, data); i += 1
        self.thread = threading.Thread(target=run, daemon=True); self.thread.start()
    def join(self): self.thread.join()
    def flush(self): self.send(3)
    def stop(self): self.send(4)
    def close(self): self.s.close()

A = Client("spotify", 0x1111)
B = Client("airplay", 0x2222)
C = Client("quiet", QUIET)

# Scenario (ms):
#   A streams 0..9000 continuously. B streams 1000..4000 loud, then keeps sending QUIET samples
#   4000..5500 (a paused AirPlay sender streaming silence), then flaps 6000..7800 (200 ms on /
#   300 ms off), then streams 8000..9000 sustained, then nothing. C streams QUIET samples the
#   whole time 0..9000.
#   Expected: A active at 400 (takeover from none); B steals at 1900 (A's hold expired, B producing
#   since 1000 >= 400); A does NOT steal back while B keeps streaming (A is the older stream);
#   B goes quiet at 4000 while still sending -> released at 4000+RELEASE, A takes over right then
#   (already producing >= 400); B's flaps never sustain 400 ms -> no transition during 6000..7800;
#   B sustained from 8000 -> steals at 8400 (A's hold expired); both stop at 9000 -> B released at
#   9000+RELEASE; idle-stop IDLE_STOP later. C never appears anywhere.
A.stream([(0, 9000)])
flaps = [(6000 + k * 500, 6000 + k * 500 + 200) for k in range(4)]  # 6000-6200, 6500-6700, 7000-7200, 7500-7700
B.stream([(1000, 4000), (4000, 5500, QUIET)] + flaps + [(8000, 9000)])
C.stream([(0, 9000)])
A.join(); B.join(); C.join()
time.sleep((IDLE_STOP + RELEASE + 800) / 1000)
A.close(); B.close(); C.close()
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
B_REL = 4000 + RELEASE
expect(0, "takeover", "none", "spotify", TAKEOVER)                 # A after 400 ms producing
expect(1, "takeover", "spotify", "airplay", TAKEOVER + HOLD)       # B at 1900: A held 1500 since 400
expect(2, "release", "airplay", "none", B_REL)                     # B quiet (still sending silence) since 4000
expect(3, "takeover", "none", "spotify", B_REL)                    # A takes over right away (producing >= 400)
expect(4, "takeover", "spotify", "airplay", 8000 + TAKEOVER)       # B sustained from 8000; no flap stole before
expect(5, "release", "airplay", "none", 9000 + RELEASE)
expect(6, "idle-stop", "none", "none", 9000 + RELEASE + IDLE_STOP)
extra = len(events) - 7
print(("ok   " if extra == 0 else "FAIL ") + f"no extra transitions (flaps 6000..7800 never stole; 'quiet' never appears): {extra} extra")
if extra: fails.append("extra")
quiet_ev = [e for e in events if "quiet" in (e[2], e[3])]
print(("ok   " if not quiet_ev else "FAIL ") + f"a source streaming silence never takes over: {quiet_ev}")
if quiet_ev: fails.append("silent-steal")
between = [e for e in events if 1900 + TOL < e[0] < 4000]
print(("ok   " if not between else "FAIL ") + f"no transition while both streamed (1900..4000): {between}")
if between: fails.append("hold")

# PCM: each active segment plays its wall-clock duration plus the LEAD the source had sent ahead.
# B's second segment (1900..B_REL) is loud only until timeline 4000+LEAD, then QUIET samples until released.
data = open(OUT, "rb").read()
frames = len(data) // 4
a_frames = data.count(struct.pack("<hh", 0x1111, 0x1111)); b_frames = data.count(struct.pack("<hh", 0x2222, 0x2222))
q_frames = data.count(struct.pack("<hh", QUIET, QUIET))
exp_a = (1900 - 400 + LEAD + 8400 - B_REL + LEAD) * 44.1
exp_b = (4000 - 1900 + LEAD + 9000 - 8400 + LEAD) * 44.1
exp_q = (B_REL - 4000 + LEAD) * 44.1   # B's own quiet tail while it was still active, plus its lead (C's quiet audio must never be played)
ok = abs(a_frames - exp_a) < 0.15 * exp_a and abs(b_frames - exp_b) < 0.15 * exp_b and abs(q_frames - exp_q) < 0.15 * exp_q \
     and a_frames + b_frames + q_frames >= frames * 0.98
print(("ok   " if ok else "FAIL ") + f"played PCM: A {a_frames} frames (expect ~{int(exp_a)}), B {b_frames} (expect ~{int(exp_b)}), "
      f"B's quiet tail {q_frames} (expect ~{int(exp_q)}; C's silence must not be played), total {frames}")
if not ok: fails.append("pcm")
act = open(f"{STATUS}/active-source").read().strip()
print(("ok   " if act == "none" else "FAIL ") + f"active-source file ends as 'none' (got '{act}')")
if act != "none": fails.append("active-source")
print("\n" + (f"{len(fails)} FAILURES" if fails else "all arbitration checks passed"))
sys.exit(1 if fails else 0)
