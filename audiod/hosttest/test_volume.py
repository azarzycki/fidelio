#!/usr/bin/env python3
"""B1.10 item 1: a hardware volume change must never let a BACKGROUND source steal the output, and the
Spotify slider must still track the true level. audiod runs in absolute mode with a mock hardware
volume. Protocol-v2 clients (read WELCOME / HWVOLUME). Usage: test_volume.py PATH_TO_fidelio-audiod
"""
import os, socket, struct, subprocess, sys, threading, time
AUDIOD = sys.argv[1]
GAP, TAKEOVER, HOLD, RELEASE, QUIET_RELEASE, IDLE_STOP, LEAD = 1000, 400, 500, 1500, 3000, 5000, 750
VOL_SLIDER, VOL_APPLY, VOL_ABSOLUTE = 1, 2, 1
HELLO, AUDIO, FLUSH, STOP, VOLUME, WELCOME, HWVOLUME = 1, 2, 3, 4, 6, 7, 8
TMP = "/tmp/audiod-vol"; os.makedirs(TMP, exist_ok=True)
SOCK, OUT, EVT, CHG = f"{TMP}/a.sock", f"{TMP}/out.pcm", f"{TMP}/ev.log", f"{TMP}/hwchange.txt"
for f in (SOCK, OUT, EVT, CHG): 
    if os.path.exists(f): os.unlink(f)
env = dict(os.environ, AUDIOD_MOCK_HW_VOLUME="0.50", AUDIOD_MOCK_HW_CHANGE_FILE=CHG)
proc = subprocess.Popen([AUDIOD, "--sock", SOCK, "--pcm-out", OUT, "--event-log", EVT, "--no-status", "--no-silence",
                         "--spotify-volume", "absolute", "--wake-on-play", "off", "--takeover-ms", str(TAKEOVER),
                         "--hold-ms", str(HOLD), "--release-ms", str(RELEASE), "--quiet-release-ms", str(QUIET_RELEASE),
                         "--idle-stop-ms", str(IDLE_STOP), "--producing-gap-ms", str(GAP), "--lead-ms", str(LEAD)],
                        stdout=open(f"{TMP}/audiod.log", "w"), stderr=subprocess.STDOUT, env=env)
for _ in range(50):
    if os.path.exists(SOCK): break
    time.sleep(0.05)
fails = []
def check(ok, msg):
    print(("ok   " if ok else "FAIL ") + msg); 
    if not ok: fails.append(msg)

class V2Client:
    def __init__(self, name, sample):
        self.s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM); self.s.connect(SOCK)
        self.s.sendall(struct.pack("<II", HELLO, 40) + struct.pack("<BBBBI32s", 2, 0, 2, 16, 44100, name.encode()))
        self.name, self.sample, self.buf, self.msgs, self.stop, self.t = name, sample, b"", [], False, None
        self.rx = threading.Thread(target=self._recv, daemon=True); self.rx.start()
    def _recv(self):
        while not self.stop:
            try: d = self.s.recv(4096)
            except OSError: break
            if not d: break
            self.buf += d
            while len(self.buf) >= 8:
                t, n = struct.unpack("<II", self.buf[:8])
                if len(self.buf) < 8 + n: break
                self.msgs.append((t, self.buf[8:8+n])); self.buf = self.buf[8+n:]
    def welcomes(self): return [m for m in self.msgs if m[0] == WELCOME]
    def hwvols(self):  return [struct.unpack("<HH", m[1]) for m in self.msgs if m[0] == HWVOLUME]
    def send(self, t, p=b""): self.s.sendall(struct.pack("<II", t, len(p)) + p)
    def set_slider(self, slider, apply=True):
        self.send(VOLUME, struct.pack("<fHH", 1.0, slider, VOL_SLIDER | (VOL_APPLY if apply else 0)))
    def play(self, ms):
        def run():
            chunk = struct.pack("<hh", self.sample, self.sample) * 882
            n = (ms + LEAD) // 20; t0 = time.monotonic()
            for i in range(n):
                if self.stop: break
                d = (i*20 - LEAD)/1000 - (time.monotonic() - t0)
                if d > 0: time.sleep(d)
                try: self.send(AUDIO, chunk)
                except OSError: break
        self.t = threading.Thread(target=run, daemon=True); self.t.start()
    def wait(self):
        if self.t: self.t.join()
    def close(self): self.stop = True; self.wait(); self.s.close()
def events(kind=None):
    ev = []
    for line in open(EVT):
        t, k, frm, to, *r = line.split()
        if kind is None or k == kind: ev.append((int(t), k, frm, to, " ".join(r)))
    return ev

# A plays first and becomes active; it is v2 so it gets a WELCOME with the 50 % level
a = V2Client("spotify", 0x1111); a.play(3500)
time.sleep(1.0)
check(len(a.welcomes()) == 1, f"spotify got one WELCOME ({len(a.welcomes())})")
w = struct.unpack("<BBHHH", a.welcomes()[0][1]) if a.welcomes() else (0,0,0,0,0)
check(w[2] == VOL_ABSOLUTE and abs(w[3] - 32768) < 400, f"WELCOME says absolute, level ~50 % (slider {w[3]})")
check(any(e[3] == "spotify" for e in events("takeover")), "spotify took over")
# B takes over (newer); A stays connected but quiet in the background
b = V2Client("airplay", 0x2222); b.play(4000)
time.sleep(1.2)
tk = events("takeover")
check(any(e[2] == "spotify" and e[3] == "airplay" for e in tk), f"airplay took over from spotify ({[e for e in tk if e[3]=='airplay']})")
a_hw_before = len(a.hwvols()); b_hw_before = len(b.hwvols())
tk_before = len(events("takeover"))
# THE BUG: a hardware volume button press while airplay is active
open(CHG, "w").write("0.70\n")
time.sleep(1.5)
# airplay (active) must stay active, no new takeover, and the background spotify must NOT be told
check(len(events("takeover")) == tk_before, f"the volume change caused NO takeover ({events('takeover')[tk_before:]})")
check(len(a.hwvols()) == a_hw_before, f"background spotify was NOT sent the hardware change (deferred): {len(a.hwvols())-a_hw_before} new HWVOLUME")
check(len(b.hwvols()) >= b_hw_before + 1 and b.hwvols()[-1][0] > 40000, f"active airplay WAS told the new level (~70 %, slider {b.hwvols()[-1][0] if b.hwvols() else 0})")
cur_active = [e for e in events() if e[1] == "release" and e[2] == "airplay"]
check(not cur_active, f"airplay was not released by the volume change ({cur_active})")
# GUARD (belt-and-suspenders): airplay was just told the level (grace armed). Its controller replying
# with a FLUSH + immediate audio within the grace window must NOT register as a recency event, so a
# background source cannot be beaten by it and airplay simply stays active with no churn.
rel_before, tk_g = len(events("release")), len(events("takeover"))
b.send(FLUSH)
time.sleep(0.05)
b.sample = 0x2222
# keep airplay producing for another second (it is already playing via play(); nudge with direct sends)
for _ in range(20):
    b.send(AUDIO, struct.pack("<hh", 0x2222, 0x2222) * 882); time.sleep(0.02)
time.sleep(0.3)
check(len(events("takeover")) == tk_g and len(events("release")) == rel_before,
      f"a FLUSH+audio from the just-reported active source caused no takeover/release (grace guard): +{len(events('takeover'))-tk_g} tk, +{len(events('release'))-rel_before} rel")
# When spotify becomes active again it must receive the current level (truthful slider)
b.close()
time.sleep(RELEASE/1000 + 0.6)
a2 = V2Client("spotify", 0x1111)  # fresh spotify start
a.close()
a2.play(2500)
time.sleep(1.2)
check(any(e[3] == "spotify" for e in events("takeover")[tk_before:]), "spotify became active again")
lvl = a2.hwvols()[-1][0] if a2.hwvols() else (struct.unpack("<BBHHH", a2.welcomes()[0][1])[3] if a2.welcomes() else 0)
check(lvl > 40000, f"the re-activated spotify learns the current ~70 % level (slider {lvl})")
a2.close()
proc.terminate(); proc.wait()
print("\nevents:")
for e in events(): print(f"  {e[0]:6d} {e[1]:9s} {e[2]:>8s} -> {e[3]:<8s} {e[4]}")
print("\n" + (f"{len(fails)} FAILURES" if fails else "all volume checks passed"))
sys.exit(1 if fails else 0)
