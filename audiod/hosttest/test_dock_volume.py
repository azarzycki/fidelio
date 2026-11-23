#!/usr/bin/env python3
"""B1.16.12: in absolute mode a SOURCE's hardware-volume set is also told to the dock (IAPAudioSetVolume
through iapd); an external change never is; nothing is told when the accessory takes no volume, when IAP
is unavailable, or in software mode; a slider drag is coalesced (the latest held level goes out once the
window ends; an external change drops a held level). audiod runs with the mock hardware volume and the
mock IAP (AUDIOD_MOCK_IAP).
B1.16.14: a source that sends a level audiod reported to it (within 3 s, one dock step) is echoing it:
nothing is set or told to the dock; a held level is dropped at its flush when the hardware level changed
outside in the meantime, even before the poll has noticed. Usage: test_dock_volume.py PATH_TO_dock-audiod
"""
import os, re, socket, struct, subprocess, sys, threading, time
AUDIOD = sys.argv[1]
VOL_SLIDER, VOL_APPLY = 1, 2
HELLO, AUDIO, VOLUME, HWVOLUME = 1, 2, 6, 8
LEAD = 750
ARB = ("--takeover-ms", "400", "--hold-ms", "500", "--release-ms", "1500", "--lead-ms", str(LEAD))
TMP = "/tmp/audiod-dockvol"; os.makedirs(TMP, exist_ok=True)
fails = []
def check(ok, msg):
    print(("ok   " if ok else "FAIL ") + msg)
    if not ok: fails.append(msg)

class Run:
    """One audiod with its own socket, event log and stdout log."""
    def __init__(self, tag, iap=None, extra=(), min_ms=None, env_extra=None):
        self.sock, self.evt, self.log, self.chg = (f"{TMP}/{tag}.sock", f"{TMP}/{tag}-ev.log", f"{TMP}/{tag}-audiod.log", f"{TMP}/{tag}-hwchange.txt")
        for f in (self.sock, self.evt, self.chg):
            if os.path.exists(f): os.unlink(f)
        env = dict(os.environ, AUDIOD_MOCK_HW_VOLUME="0.50", AUDIOD_MOCK_HW_CHANGE_FILE=self.chg)
        env.pop("AUDIOD_MOCK_IAP", None)
        env.pop("AUDIOD_DOCK_FORWARD_MIN_MS", None)
        env.pop("AUDIOD_MOCK_HW_POLL_MS", None)
        env.update(env_extra or {})
        if iap: env["AUDIOD_MOCK_IAP"] = iap
        if min_ms is not None: env["AUDIOD_DOCK_FORWARD_MIN_MS"] = str(min_ms)
        self.p = subprocess.Popen([AUDIOD, "--sock", self.sock, "--pcm-out", f"{TMP}/{tag}-out.pcm", "--event-log", self.evt,
                                   "--no-status", "--no-silence", "--wake-on-play", "off", *extra],
                                  stdout=open(self.log, "w"), stderr=subprocess.STDOUT, env=env)
        for _ in range(100):
            if os.path.exists(self.sock): break
            time.sleep(0.05)
        self.clients = {}
        self.hw = {}       # name -> HWVOLUME sliders received (B1.16.14)
        self.stopping = False
        self.lock = threading.Lock()
    def client(self, name):
        if name not in self.clients:
            s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM); s.connect(self.sock)
            s.sendall(struct.pack("<II", HELLO, 40) + struct.pack("<BBBBI32s", 2, 0, 2, 16, 44100, name.encode()))
            self.clients[name] = s
            self.hw[name] = []
            threading.Thread(target=self._recv, args=(name, s), daemon=True).start()
        return self.clients[name]
    def _recv(self, name, s):  # read WELCOME / HWVOLUME so nothing piles up, and keep the reported sliders
        buf = b""
        while not self.stopping:
            try: d = s.recv(4096)
            except OSError: break
            if not d: break
            buf += d
            while len(buf) >= 8:
                t, n = struct.unpack("<II", buf[:8])
                if len(buf) < 8 + n: break
                if t == HWVOLUME: self.hw[name].append(struct.unpack("<HH", buf[8:12])[0])
                buf = buf[8 + n:]
    def play(self, name, ms):  # B1.16.14: make the source the active one (only the active source gets reports)
        s = self.client(name)
        def run():
            chunk = struct.pack("<hh", 0x1111, 0x1111) * 882
            n = (ms + LEAD) // 20; t0 = time.monotonic()
            for i in range(n):
                if self.stopping: break
                d = (i * 20 - LEAD) / 1000 - (time.monotonic() - t0)
                if d > 0: time.sleep(d)
                try:
                    with self.lock: s.sendall(struct.pack("<II", AUDIO, len(chunk)) + chunk)
                except OSError: break
        threading.Thread(target=run, daemon=True).start()
    def slider(self, name, level):  # the linear curve (the default): slider = level * 65535
        self.slider_raw(name, int(round(level * 65535)))
    def slider_raw(self, name, s):
        with self.lock: self.client(name).sendall(struct.pack("<II", VOLUME, 8) + struct.pack("<fHH", 1.0, s, VOL_SLIDER | VOL_APPLY))
    def buttons(self, level):  # an external change (the dock's buttons / another app)
        open(self.chg, "w").write((level if isinstance(level, str) else f"{level:.2f}") + "\n")
    def stop(self):
        self.stopping = True
        with self.lock:
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

# ---------------------------------------------------------------- 5. B1.16.14: a source echoing a reported level
# Device-found: an AirPlay sender sends every DACP dmcp.device-volume straight back as a volume command;
# a late echo undid a press on the dock's buttons and was told to the dock. AirPlay level = dB / 30 + 1.
r = Run("echo", iap="ok", extra=ARB)
time.sleep(0.3)
r.play("airplay", 13000); time.sleep(1.5)                  # active (the takeover report: 0.50)
# (a) an outside change, reported, echoed back with the same slider
n0 = len(r.hw["airplay"])
r.buttons(0.70); time.sleep(0.4)
rep_a = r.hw["airplay"][n0:]
r.slider("airplay", 0.70); time.sleep(0.4)
# (b) three outside changes in a row, then their three echoes, late and in order
n1 = len(r.hw["airplay"])
r.buttons(0.60); time.sleep(0.3)
r.buttons(0.65); time.sleep(0.3)
r.buttons(0.62); time.sleep(0.3)
rep_b = r.hw["airplay"][n1:]
r.slider("airplay", 0.60); time.sleep(0.05)
r.slider("airplay", 0.65); time.sleep(0.05)
r.slider("airplay", 0.62); time.sleep(0.6)
# (c) an echo off by the sender's 0.1 dB rounding: 0.345 = -19.65 dB, echoed as -19.7 dB = 0.3433
r.buttons("0.345"); time.sleep(0.4)
r.slider_raw("airplay", int(round((1 - 19.7 / 30) * 65535))); time.sleep(0.3)
# (d) a different level right after the report: a user move
r.slider("airplay", 0.42); time.sleep(3.2)
# (e) the reported level again, after the 3 s window, while the level is elsewhere: a user move
r.slider("airplay", 0.345); time.sleep(0.5)
r.stop()
ev, log, calls = r.events(), r.text(), r.calls()
air = [e for e in ev if e[1] == "airplay"]
ext = [e[2] for e in ev if e[1] == "external"]
s70 = int(round(0.70 * 65535))
check(any(abs(v - s70) <= 1 for v in rep_a), f"(a) the outside change 0.70 was reported to the active source ({rep_a})")
check("hwvolume: 'airplay' echoed 0.700 (reported " in log and not any(e[2] == "0.700" for e in air) and 0.7 not in calls,
      f"(a) its echo is ignored: no set, no dock call, one 'echoed' line (sets {[e[2] for e in air]}, calls {calls})")
check(len(rep_b) >= 3, f"(b) the three outside changes were reported ({rep_b})")
check(not any(e[2] in ("0.600", "0.650", "0.620") for e in air) and not any(c in (0.6, 0.65, 0.62) for c in calls)
      and len(re.findall(r"'airplay' echoed 0\.6[025]0 ", log)) == 3,
      f"(b) none of the three late echoes is applied or told to the dock (sets {[e[2] for e in air]}, calls {calls})")
check(ext[:5] == ["0.700", "0.600", "0.650", "0.620", "0.345"],
      f"(b) the level stays at the last outside value: no external change follows from an echo ({ext})")
check(re.search(r"'airplay' echoed 0\.343 \(reported \d+ ms ago\): ignored", log) is not None
      and not any(e[2] in ("0.343", "0.344") for e in air),
      f"(c) an echo 0.1 dB off the report is still recognised (sets {[e[2] for e in air]})")
check(len(air) == 2 and air[0][2] == "0.420" and air[0][3].endswith("dock=ok") and calls[:1] == [0.42],
      f"(d) a different level right after a report is applied and told to the dock ({[(e[2], e[3]) for e in air]}, calls {calls})")
check(len(air) == 2 and air[1][2] == "0.345" and air[1][3].endswith("dock=ok") and calls == [0.42, 0.345],
      f"(e) the reported level again after the 3 s window, from elsewhere, is applied and told to the dock (calls {calls})")
check(not any("echoed" in e[3] for e in ev), "an echo is never written to the event log as a volume change")

# ---------------------------------------------------------------- 6. B1.16.14: a held level after an unseen outside change
# The hardware level changes (the dock's buttons) after a held set, and the flush comes before the poll
# (3 s here; the device polls every 1 s and its notification can come late) has reported it: the held
# level must not go out.
r = Run("flushrace", iap="ok", min_ms=600, env_extra={"AUDIOD_MOCK_HW_POLL_MS": "3000"})
time.sleep(0.3)
r.slider("spotify", 0.40); time.sleep(0.05)                # told at once
r.slider("spotify", 0.45); time.sleep(0.1)                 # held
r.buttons(0.30); time.sleep(3.6)                           # the hardware changed; the poll has not seen it at the flush
r.stop()
ev, log, calls = r.events(), r.text(), r.calls()
ext = [e for e in ev if e[1] == "external"]
check(calls == [0.4] and "held dock level 0.450 dropped (1 held): the level changed outside (now 0.300, not yet reported)" in log,
      f"(f) the held level is dropped at its flush: the hardware changed outside first (calls {calls})")
check([e[2] for e in ext] == ["0.300"], f"(f) the outside change is then reported by the normal path ({ext})")

print(f"\n{len(fails)} FAIL" if fails else "\nall ok")
sys.exit(1 if fails else 0)
