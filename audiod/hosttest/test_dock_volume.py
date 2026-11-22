#!/usr/bin/env python3
"""B1.16.12: in absolute mode a SOURCE's hardware-volume set is also told to the dock (IAPAudioSetVolume
through iapd); an external change never is; nothing is told when the accessory takes no volume, when IAP
is unavailable, or in software mode; a slider drag is coalesced (the latest held level goes out once the
window ends; an external change drops a held level). audiod runs with the mock hardware volume and the
mock IAP (AUDIOD_MOCK_IAP). Usage: test_dock_volume.py PATH_TO_fidelio-audiod
"""
import os, re, socket, struct, subprocess, sys, time
AUDIOD = sys.argv[1]
VOL_SLIDER, VOL_APPLY = 1, 2
HELLO, VOLUME = 1, 6
TMP = "/tmp/audiod-dockvol"; os.makedirs(TMP, exist_ok=True)
fails = []
def check(ok, msg):
    print(("ok   " if ok else "FAIL ") + msg)
    if not ok: fails.append(msg)

class Run:
    """One audiod with its own socket, event log and stdout log."""
    def __init__(self, tag, iap=None, extra=(), min_ms=None):
        self.sock, self.evt, self.log, self.chg = (f"{TMP}/{tag}.sock", f"{TMP}/{tag}-ev.log", f"{TMP}/{tag}-audiod.log", f"{TMP}/{tag}-hwchange.txt")
        for f in (self.sock, self.evt, self.chg):
            if os.path.exists(f): os.unlink(f)
        env = dict(os.environ, AUDIOD_MOCK_HW_VOLUME="0.50", AUDIOD_MOCK_HW_CHANGE_FILE=self.chg)
        env.pop("AUDIOD_MOCK_IAP", None)
        env.pop("AUDIOD_DOCK_FORWARD_MIN_MS", None)
        if iap: env["AUDIOD_MOCK_IAP"] = iap
        if min_ms is not None: env["AUDIOD_DOCK_FORWARD_MIN_MS"] = str(min_ms)
        self.p = subprocess.Popen([AUDIOD, "--sock", self.sock, "--pcm-out", f"{TMP}/{tag}-out.pcm", "--event-log", self.evt,
                                   "--no-status", "--no-silence", "--wake-on-play", "off", *extra],
                                  stdout=open(self.log, "w"), stderr=subprocess.STDOUT, env=env)
        for _ in range(100):
            if os.path.exists(self.sock): break
            time.sleep(0.05)
        self.clients = {}
    def client(self, name):
        if name not in self.clients:
            s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM); s.connect(self.sock)
            s.sendall(struct.pack("<II", HELLO, 40) + struct.pack("<BBBBI32s", 2, 0, 2, 16, 44100, name.encode()))
            self.clients[name] = s
        return self.clients[name]
    def slider(self, name, level):  # the linear curve (the default): slider = level * 65535
        s = int(round(level * 65535))
        self.client(name).sendall(struct.pack("<II", VOLUME, 8) + struct.pack("<fHH", 1.0, s, VOL_SLIDER | VOL_APPLY))
    def buttons(self, level):  # an external change (the dock's buttons / another app)
        open(self.chg, "w").write(f"{level:.2f}\n")
    def stop(self):
        for s in self.clients.values(): s.close()
        self.p.terminate(); self.p.wait()
    def events(self):
        ev = []
        for line in open(self.evt):
            t, k, frm, to, *r = line.split()
            if k == "hwvolume": ev.append((int(t), frm, to, " ".join(r)))
        return ev
    def text(self): return open(self.log).read()
    def calls(self): return [float(v) for v in re.findall(r"mock iap: IAPAudioSetVolume\(([0-9.]+)\) -> 1", self.text())]

# ---------------------------------------------------------------- 1. the dock takes the volume (window 1000 ms)
r = Run("ok", iap="ok", min_ms=1000)
time.sleep(0.3)
r.slider("spotify", 0.40); time.sleep(1.2)                 # a single move: told at once
r.buttons(0.70); time.sleep(0.5)                           # the dock's buttons: never echoed back
r.slider("spotify", 0.20); time.sleep(0.05)                # a drag: the first goes out ...
r.slider("spotify", 0.25); time.sleep(0.05)                # ... the rest are held ...
r.slider("spotify", 0.30); time.sleep(2.2)                 # ... and the latest goes out when the window ends
r.slider("spotify", 0.50); time.sleep(0.05)                # told at once (the window passed)
r.slider("spotify", 0.55); time.sleep(0.15)                # held ...
r.buttons(0.80); time.sleep(1.6)                           # ... and dropped by an external change
r.stop()
ev, log, calls = r.events(), r.text(), r.calls()
src = [e for e in ev if e[1] == "spotify"]
ext = [e for e in ev if e[1] == "external"]
check("dock volume (B1.16.12): available" in log, "start-up line: the dock forward is available (mock IAP)")
check(len(src) >= 1 and src[0][2] == "0.400" and src[0][3].endswith("; dock=ok") and calls[:1] == [0.4],
      f"a Spotify slider set in absolute mode is told to the dock with the same level ({src[:1]}, calls {calls})")
check(len(ext) == 2 and all("dock=" not in e[3] for e in ext) and [e[2] for e in ext] == ["0.700", "0.800"],
      f"the external changes are reported without a dock forward ({ext})")
check(0.7 not in calls and 0.8 not in calls, f"no external level was ever sent to the dock (calls {calls})")
drag = [e[3].split("dock=")[-1] for e in src[1:4]]
check(drag == ["ok", "deferred", "deferred"], f"a drag: the first set is told, the next two inside the window are held ({drag})")
check(calls[1:3] == [0.2, 0.3] and "dock told 0.300, the latest of 2 held sets: dock=ok" in log,
      f"the latest held level (0.300) went out once the window ended, the intermediate 0.250 never did (calls {calls})")
check(len(src) == 6 and src[4][3].endswith("dock=ok") and src[5][3].endswith("dock=deferred") and calls[3:] == [0.5]
      and "held dock level 0.550 dropped" in log,
      f"a held level is dropped when an external change comes before the window ends (calls {calls})")

# ---------------------------------------------------------------- 2. no accessory takes the volume / the query fails / the call fails
for mode, word in (("unsupported", "unsupported"), ("query-fail", "query-failed"), ("fail", "failed")):
    r = Run(mode, iap=mode)
    time.sleep(0.3); r.slider("spotify", 0.40); time.sleep(0.5); r.stop()
    ev = [e for e in r.events() if e[1] == "spotify"]
    made = "mock iap: IAPAudioSetVolume" in r.text()
    expect_call = mode == "fail"
    check(len(ev) == 1 and ev[0][2] == "0.400" and ev[0][3].endswith("; dock=" + word) and made == expect_call,
          f"AUDIOD_MOCK_IAP={mode}: the system volume is set, the line says dock={word}, IAPAudioSetVolume {'called' if expect_call else 'not called'} ({ev})")

# ---------------------------------------------------------------- 3. IAP unavailable (the framework or a symbol missing)
r = Run("none")
time.sleep(0.3); r.slider("spotify", 0.40); time.sleep(0.5); r.stop()
ev, log = [e for e in r.events() if e[1] == "spotify"], r.text()
check("dock volume (B1.16.12): unavailable" in log, "start-up line: the dock forward is unavailable without IAP")
check(len(ev) == 1 and ev[0][2] == "0.400" and ev[0][3].endswith("; dock=unavailable") and "mock iap:" not in log,
      f"IAP unavailable: the system volume is still set, nothing is sent ({ev})")

# ---------------------------------------------------------------- 4. per-source mode: Spotify software, AirPlay absolute
r = Run("mixed", iap="ok", extra=("--spotify-volume", "software", "--airplay-volume-mode", "absolute"))
time.sleep(0.3)
r.slider("spotify", 0.40); time.sleep(0.4)
r.slider("airplay", 0.60); time.sleep(0.5)
r.stop()
ev, calls = r.events(), r.calls()
check(not any(e[1] == "spotify" for e in ev) and 0.4 not in calls, f"software mode: a Spotify slider move neither sets the hardware nor tells the dock ({ev}, calls {calls})")
check([e[2] for e in ev if e[1] == "airplay"] == ["0.600"] and calls == [0.6] and ev[-1][3].endswith("; dock=ok"),
      f"AirPlay in absolute mode: its volume set is told to the dock ({ev}, calls {calls})")

print(f"\n{len(fails)} FAIL" if fails else "\nall ok")
sys.exit(1 if fails else 0)
