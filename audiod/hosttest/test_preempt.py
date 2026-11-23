#!/usr/bin/env python3
"""B1.12 part A: a source that loses the output is told to pause (PREEMPTED), so a background session
cannot roll on and steal it back. Protocol-v2 clients. Usage: test_preempt.py PATH_TO_dock-audiod
"""
import os, socket, struct, subprocess, sys, threading, time
AUDIOD = sys.argv[1]
GAP, TAKEOVER, HOLD, RELEASE, QUIET_RELEASE, IDLE_STOP, LEAD = 1000, 400, 500, 1500, 3000, 5000, 750
HELLO, AUDIO, FLUSH, STOP, VOLUME, WELCOME, HWVOLUME, PREEMPTED = 1, 2, 3, 4, 6, 7, 8, 9
TMP = "/tmp/audiod-preempt"; os.makedirs(TMP, exist_ok=True)
fails = []
def check(ok, msg):
    print(("ok   " if ok else "FAIL ") + msg)
    if not ok: fails.append(msg)

def start(extra=None):
    sock, out, evt = f"{TMP}/a.sock", f"{TMP}/out.pcm", f"{TMP}/ev.log"
    for f in (sock, out, evt):
        if os.path.exists(f): os.unlink(f)
    args = [AUDIOD, "--sock", sock, "--pcm-out", out, "--event-log", evt, "--no-status", "--no-silence",
            "--spotify-volume", "software", "--wake-on-play", "off", "--takeover-ms", str(TAKEOVER), "--hold-ms", str(HOLD),
            "--release-ms", str(RELEASE), "--quiet-release-ms", str(QUIET_RELEASE), "--idle-stop-ms", str(IDLE_STOP),
            "--producing-gap-ms", str(GAP), "--lead-ms", str(LEAD)] + (extra or [])
    p = subprocess.Popen(args, stdout=open(f"{TMP}/audiod.log", "w"), stderr=subprocess.STDOUT)
    for _ in range(50):
        if os.path.exists(sock): break
        time.sleep(0.05)
    return p, sock, evt

class V2Client:
    def __init__(self, sock, name, sample):
        self.s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM); self.s.connect(sock)
        self.s.sendall(struct.pack("<II", HELLO, 40) + struct.pack("<BBBBI32s", 2, 0, 2, 16, 44100, name.encode()))
        self.name, self.sample, self.buf, self.preempts, self.stop, self.t = name, sample, b"", [], False, None
        threading.Thread(target=self._rx, daemon=True).start()
    def _rx(self):
        while not self.stop:
            try: d = self.s.recv(4096)
            except OSError: break
            if not d: break
            self.buf += d
            while len(self.buf) >= 8:
                t, n = struct.unpack("<II", self.buf[:8])
                if len(self.buf) < 8 + n: break
                pl = self.buf[8:8+n]; self.buf = self.buf[8+n:]
                if t == PREEMPTED: self.preempts.append(pl.split(b"\x00")[0].decode())
    def send(self, t, p=b""): 
        try: self.s.sendall(struct.pack("<II", t, len(p)) + p)
        except OSError: pass
    def play(self, ms):
        def run():
            chunk = struct.pack("<hh", self.sample, self.sample) * 882
            n = (ms + LEAD) // 20; t0 = time.monotonic()
            for i in range(n):
                if self.stop: break
                d = (i*20 - LEAD)/1000 - (time.monotonic() - t0)
                if d > 0: time.sleep(d)
                self.send(AUDIO, chunk)
        self.t = threading.Thread(target=run, daemon=True); self.t.start()
    def wait(self):
        if self.t: self.t.join()
    def close(self): self.stop = True; self.wait(); self.s.close()
def events(evt, kind=None):
    ev = []
    for line in open(evt):
        t, k, frm, to, *r = line.split()
        if kind is None or k == kind: ev.append((int(t), k, frm, to, " ".join(r)))
    return ev

# ---- scenario 1: spotify active, airplay takes over -> spotify is told to pause; airplay is not
proc, sock, evt = start()
a = V2Client(sock, "spotify", 0x1111); a.play(3000)
time.sleep(1.0)
b = V2Client(sock, "airplay", 0x2222); b.play(5000)
time.sleep(1.2)
check(any(e[2] == "spotify" and e[3] == "airplay" for e in events(evt, "takeover")), "airplay took over from spotify")
time.sleep(0.8)  # past the 500 ms preempt delay
check(a.preempts == ["airplay"], f"spotify (the loser) received exactly one PREEMPTED naming airplay ({a.preempts})")
check(b.preempts == [], f"airplay (the active source) received no PREEMPTED ({b.preempts})")
# spotify reacts like cspot: stop feeding (pause). airplay must stay active, no takeover back.
a.stop = True; a.wait()
tk_before = len(events(evt, "takeover"))
time.sleep(1.8)
check(len(events(evt, "takeover")) == tk_before, "a paused (silent) spotify does not take the output back")
# ---- resume: spotify plays again after the gap -> new stream -> takes over -> airplay is pre-empted
a2 = V2Client(sock, "spotify", 0x3333); a2.play(3000)
time.sleep(1.2)
check(any(e[2] == "airplay" and e[3] == "spotify" for e in events(evt, "takeover")), "spotify resume takes the output back (new stream)")
time.sleep(0.8)
check(b.preempts == ["spotify"], f"airplay is now told to pause (PREEMPTED by spotify) ({b.preempts})")
a2.close(); b.close(); proc.terminate(); proc.wait()

# ---- scenario 2: flap guard -- a takeover that reverses before the preempt fires must not pause the
# source that is active again. Long preempt delay (2 s) + no hold, so spotify can reclaim first.
proc, sock, evt = start(["--hold-ms", "0", "--preempt-delay-ms", "2000"])
a = V2Client(sock, "spotify", 0x1111); a.play(6000)
time.sleep(1.0)
b = V2Client(sock, "airplay", 0x2222); b.play(6000)
time.sleep(0.9)  # airplay takes over (newer); spotify preempt is scheduled 2 s out
check(any(e[3] == "airplay" for e in events(evt, "takeover")), "airplay took over (flap setup)")
# spotify starts a fresh stream (FLUSH + loud) -> becomes newest -> reclaims within ~0.5 s, before the 2 s preempt
a.send(FLUSH)
time.sleep(1.0)  # ~0.4 s of loud makes spotify the active source again, still inside the 2 s window
check(events(evt, "takeover")[-1][3] == "spotify", f"spotify reclaimed the output before its preempt could fire ({events(evt,'takeover')[-2:]})")
time.sleep(1.8)  # let the 2 s preempt deadline pass
check(a.preempts == [], f"spotify was NOT told to pause -- it is active again (flap guard) ({a.preempts})")
check(b.preempts == ["spotify"], f"airplay (the one that actually lost) was told to pause ({b.preempts})")
a.close(); b.close(); proc.terminate(); proc.wait()

# ---- scenario 3: --preempt-pause off -> no PREEMPTED at all
proc, sock, evt = start(["--preempt-pause", "off"])
a = V2Client(sock, "spotify", 0x1111); a.play(3000)
time.sleep(1.0)
b = V2Client(sock, "airplay", 0x2222); b.play(3000)
time.sleep(2.0)
check(a.preempts == [] and b.preempts == [], f"--preempt-pause off: no PREEMPTED sent ({a.preempts}, {b.preempts})")
a.close(); b.close(); proc.terminate(); proc.wait()

print("\n" + (f"{len(fails)} FAILURES" if fails else "all preempt checks passed"))
sys.exit(1 if fails else 0)
