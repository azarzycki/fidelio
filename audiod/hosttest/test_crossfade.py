#!/usr/bin/env python3
"""Host test for B1.6: a source switch and a release must not click.

  test_crossfade.py PATH_TO_fidelio-audiod

audiod runs with the paced output model (--pcm-out-paced 0: the real sink's ring and consumption,
so what the file holds is what the speaker would have played). A streams a 220 Hz sine, B a 330 Hz
sine (amplitude 10000, SocketSink-style lead). B takes over from A, later both stop (A is released).
With --crossfade-ms 30 the output must have no sample-to-sample step larger than the sines' own
maximum slope anywhere (no click at the takeover, none at the release), and B must be playing
cleanly after the window. With --crossfade-ms 0 (the old hard flush) the same scenario is run for
comparison and the step it produces is printed.
"""
import math, os, socket, struct, subprocess, sys, threading, time

AUDIOD = sys.argv[1] if len(sys.argv) > 1 else "build/fidelio-audiod"
LEAD, AMP = 750, 10000
TMP = "/tmp/audiod-crossfade"
os.makedirs(TMP, exist_ok=True)
fails = []
def check(ok, msg):
    print(("ok   " if ok else "FAIL ") + msg)
    if not ok: fails.append(msg)

class Client:
    def __init__(self, sock, name, freq):
        self.freq = freq
        self.s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        for attempt in range(50):
            try: self.s.connect(sock); break
            except (ConnectionRefusedError, FileNotFoundError):
                if attempt == 49: raise
                time.sleep(0.05)
        self.send(1, struct.pack("<BBBBI32s", 1, 0, 2, 16, 44100, name.encode()))
    def send(self, t, payload=b""): self.s.sendall(struct.pack("<II", t, len(payload)) + payload)
    def chunk(self, i):
        return b"".join(struct.pack("<hh", v, v) for v in (int(AMP * math.sin(2 * math.pi * self.freq * (i * 882 + k) / 44100.0)) for k in range(882)))
    def stream(self, t0, a, b):
        def run():
            i = a // 20
            while i * 20 < b:
                d = max(a, i * 20 - LEAD) / 1000 - (time.monotonic() - t0)
                if d > 0: time.sleep(d)
                self.send(2, self.chunk(i)); i += 1
        self.th = threading.Thread(target=run, daemon=True); self.th.start()
    def join(self): self.th.join()
    def close(self): self.s.close()

def run(tag, crossfade_ms):
    sock, out, evt = f"{TMP}/{tag}.sock", f"{TMP}/{tag}.pcm", f"{TMP}/{tag}.events"
    for f in (sock, out, evt):
        if os.path.exists(f): os.unlink(f)
    p = subprocess.Popen([AUDIOD, "--sock", sock, "--pcm-out", out, "--pcm-out-paced", "0", "--event-log", evt, "--no-status", "--no-silence",
                          "--crossfade-ms", str(crossfade_ms), "--idle-stop-ms", "1500"],
                         stdout=open(f"{TMP}/{tag}.log", "w"), stderr=subprocess.STDOUT)
    for _ in range(50):
        if os.path.exists(sock): break
        time.sleep(0.05)
    A = Client(sock, "spotify", 220.0)
    B = Client(sock, "airplay", 330.0)
    t0 = time.monotonic()
    A.stream(t0, 0, 6000)        # A: 0..6 s
    B.stream(t0, 2000, 6000)     # B: 2..6 s, newer -> takes over at 2.4 s; both stop at 6 s -> A... B released (quiet) at 6-0.75+1.5
    A.join(); B.join()
    time.sleep(3.5)
    A.close(); B.close(); time.sleep(0.3); p.terminate(); p.wait()
    data = open(out, "rb").read()
    left = struct.unpack("<%dh" % (len(data) // 2), data)[0::2]
    ev = [l.split() for l in open(evt)]
    return left, ev

# the sines' own maximum sample-to-sample step: AMP * 2*pi*f/44100 (330 Hz -> 470); the mix of both can
# reach the sum; a hard cut can jump by up to 2*AMP
NATURAL = int(AMP * 2 * math.pi * 330 / 44100) + int(AMP * 2 * math.pi * 220 / 44100) + 50

def max_step(left, lo, hi):
    lo = max(lo, 1); hi = min(hi, len(left))
    best, at = 0, -1
    for i in range(lo, hi):
        d = abs(left[i] - left[i - 1])
        if d > best: best, at = d, i
    return best, at

for tag, ms in (("hardflush", 0), ("crossfade", 30)):
    left, ev = run(tag, ms)
    tk = [e for e in ev if e[1] == "takeover" and e[3] == "airplay"]
    rl = [e for e in ev if e[1] == "release"]
    print(f"== {tag} (--crossfade-ms {ms}): {len(left)} frames, takeover at {tk[0][0] if tk else '?'} ms, release at {rl[0][0] if rl else '?'} ms")
    step, at = max_step(left, 0, len(left))
    print(f"   max sample-to-sample step over the whole output: {step} at frame {at} ({at / 44.1:.0f} ms); natural maximum {NATURAL}")
    if ms == 0:
        print(f"   (a hard flush is allowed to click: {'clicked' if step > NATURAL else 'did not click this time'})")
        continue
    check(len(tk) == 1 and len(rl) == 1, f"one takeover and one release ({len(tk)}, {len(rl)})")
    check(step <= NATURAL, f"crossfade: no step above the sines' own slope anywhere in the output ({step} <= {NATURAL})")
    # B plays cleanly after the window: the segment 1 s after the takeover is a pure 330 Hz sine at full amplitude
    t_take = int(tk[0][0]) if tk else 0
    seg = left[int((t_take + 1000) * 44.1):int((t_take + 1500) * 44.1)]
    zc = sum(1 for a, b in zip(seg, seg[1:]) if a >= 0 and b < 0)
    check(abs(zc - 165) <= 4 and max(seg) >= AMP * 0.97, f"B plays cleanly after the switch: {zc} cycles in 0.5 s (expect 165), peak {max(seg) if seg else 0}")
    # A played before the takeover: 220 Hz (the output's clock starts at the first write, ~0.45 s
    # after audiod's event clock, so stay well clear of the switch)
    seg = left[int((t_take - 1500) * 44.1):int((t_take - 1000) * 44.1)]
    zc = sum(1 for a, b in zip(seg, seg[1:]) if a >= 0 and b < 0)
    check(abs(zc - 110) <= 4, f"A played before the switch: {zc} cycles in 0.5 s (expect 110)")
    # the release: the output after the release ramps to silence (the tail faded) and stays there
    t_rel = int(rl[0][0]) if rl else 0
    tail = left[int((t_rel + 100) * 44.1):int((t_rel + 600) * 44.1)]
    check(tail and max(abs(v) for v in tail) == 0, f"after the release the output is silent (max |sample| {max(abs(v) for v in tail) if tail else 'n/a'})")
print("\n" + (f"{len(fails)} FAILURES" if fails else "all crossfade checks passed"))
sys.exit(1 if fails else 0)
