#!/usr/bin/env python3
"""Host test of unlock-on-play (B1.16.4, B1.16.6) and its precedence over the lock plugin.

When playback starts from nothing, audiod wakes the display and launches NowPlaying (B1.9). If that
launch is refused because the device is locked (SpringBoard code 3), audiod asks SpringBoard to unlock
and open the app's URL (SBSOpenSensitiveURLAndUnlock). B1.16.6: an accepted request IS the result. On
the 5.1.1 iPad it unlocks into NowPlaying while SpringBoard keeps answering 3 to launches for seconds,
so there is no launch retry and no timeout any more. The lock plugin (B1.11) is enabled only when audiod
does not unlock the device for that start. The host build's mock waker plays a locked device (env
AUDIOD_MOCK_LOCKED / AUDIOD_MOCK_UNLOCK) and logs every call ("mock waker: ..."); audiod logs every
decision to the event log (kind "wake"). Each scenario runs its own audiod.
Usage: test_unlock.py PATH_TO_fidelio-audiod
"""
import os, re, socket, struct, subprocess, sys, threading, time

AUDIOD = sys.argv[1]
TAKEOVER, HOLD, RELEASE, IDLE_STOP, LEAD = 400, 1500, 1000, 2000, 750
TMP = "/tmp/audiod-unlock"
os.makedirs(TMP, exist_ok=True)
fails = []


def check(ok, msg):
    print(("ok   " if ok else "FAIL ") + msg)
    if not ok:
        fails.append(msg)


def hello(name):
    return struct.pack("<BBBBI32s", 1, 0, 2, 16, 44100, name.encode())


class Client:
    def __init__(self, sock, name, sample):
        self.s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        for attempt in range(50):
            try:
                self.s.connect(sock)
                break
            except (ConnectionRefusedError, FileNotFoundError):
                if attempt == 49:
                    raise
                time.sleep(0.05)
        self.s.sendall(struct.pack("<II", 1, 40) + hello(name))
        self.sample, self.t = sample, None

    def play(self, ms):
        """stream `ms` of loud audio in real time (plus the LEAD burst), then go quiet"""
        def run():
            chunk = struct.pack("<hh", self.sample, self.sample) * 882
            t0 = time.monotonic()
            for i in range((ms + LEAD) // 20):
                d = (i * 20 - LEAD) / 1000 - (time.monotonic() - t0)
                if d > 0:
                    time.sleep(d)
                try:
                    self.s.sendall(struct.pack("<II", 2, len(chunk)) + chunk)
                except OSError:
                    break
        self.t = threading.Thread(target=run, daemon=True)
        self.t.start()

    def close(self):
        if self.t:
            self.t.join()
        self.s.close()


def scenario(name, env, args, plays, total_ms):
    """plays: [(start_ms, client_name, play_ms)]; returns (wake events, all events, audiod log text)"""
    d = f"{TMP}/{name}"
    os.makedirs(d, exist_ok=True)
    sock, out, evt, log = f"{d}/audiod.sock", f"{d}/out.pcm", f"{d}/events.log", f"{d}/audiod.log"
    for f in (sock, out, evt):
        if os.path.exists(f):
            os.unlink(f)
    e = dict(os.environ)
    for k in ("AUDIOD_MOCK_LOCKED", "AUDIOD_MOCK_UNLOCK"):
        e.pop(k, None)
    e.update(env)
    proc = subprocess.Popen([AUDIOD, "--sock", sock, "--pcm-out", out, "--event-log", evt, "--no-status", "--no-silence",
                             "--takeover-ms", str(TAKEOVER), "--hold-ms", str(HOLD), "--release-ms", str(RELEASE),
                             "--idle-stop-ms", str(IDLE_STOP), "--lead-ms", str(LEAD), "--wake-on-play", "on",
                             "--wake-bundle", "com.fidelio.nowplaying", "--lock-plugin", "fidelio"] + args,
                            stdout=open(log, "w"), stderr=subprocess.STDOUT, env=e)
    for _ in range(50):
        if os.path.exists(sock):
            break
        time.sleep(0.05)
    t0 = time.monotonic()
    clients = []
    for start, cname, ms in plays:
        dly = start / 1000 - (time.monotonic() - t0)
        if dly > 0:
            time.sleep(dly)
        c = Client(sock, cname, 0x1111 + 0x1111 * len(clients))
        c.play(ms)
        clients.append(c)
    dly = total_ms / 1000 - (time.monotonic() - t0)
    if dly > 0:
        time.sleep(dly)
    for c in clients:
        c.close()
    proc.terminate()
    proc.wait()
    events = []
    for line in open(evt):
        t, k, frm, to, *reason = line.split()
        events.append((int(t), k, frm, to, " ".join(reason)))
    text = open(log).read()
    print(f"-- {name}: " + "; ".join(f"{ev[0]} {ev[4]}" for ev in events if ev[1] == "wake"))
    return [ev for ev in events if ev[1] == "wake"], events, text


def has(evs, prefix):
    return [ev for ev in evs if ev[4].startswith(prefix)]


def count(text, rx):
    return len(re.findall(rx, text, re.M))


UNLOCK_CALL = r"mock waker: openURLAndUnlock\("
# B1.16.6: the start's wake is the only launch. Counts B1.16.4's retry calls ("launch(") as well, so any
# probe after an unlock request, under either name, fails the checks below.
WAKE_CALL = r"mock waker: (?:wake|launch)\("

# A. not locked: the launch succeeds, no unlock is attempted, the lock plugin is enabled as before.
w, _, log = scenario("unlocked", {}, [], [(0, "spotify", 1500)], 3000)
check(len(has(w, "wake+launch com.fidelio.nowplaying")) == 1, "unlocked: the wake's launch succeeded (wake+launch)")
check(not [ev for ev in w if "unlock" in ev[4]] and count(log, UNLOCK_CALL) == 0, "unlocked: no unlock requested (launch 0 -> no unlock call)")
lp = has(w, "lock-plugin enabled fidelio")
check(len(lp) == 1, "unlocked: the lock plugin is enabled (audiod did not unlock the device)")

# B. locked, SpringBoard accepts the unlock request: that is the result (B1.16.6). No launch retry, no
#    timeout, and the lock plugin stays off (agreed rule: audiod unlocked the device, the app is in front).
w, _, log = scenario("unlock-ok", {"AUDIOD_MOCK_LOCKED": "1", "AUDIOD_MOCK_UNLOCK": "ok"}, [], [(0, "spotify", 2000)], 3000)
check(len(has(w, "wake: device locked, unlock accepted (nowplaying:)")) == 1, "locked: launch 3 -> unlock requested with the app's URL nowplaying: and accepted")
check(count(log, UNLOCK_CALL + r"nowplaying:\) -> 1") == 1 and count(log, UNLOCK_CALL) == 1, "locked: exactly one SBSOpenSensitiveURLAndUnlock call")
check(count(log, WAKE_CALL) == 1 and not has(w, "unlock not confirmed") and not has(w, "unlocked:") and not has(w, "unlock wait"),
      f"accepted: no launch retry and no confirmation wait ({count(log, WAKE_CALL)} launch)")
check(not has(w, "lock-plugin enabled"), "locked + unlocked by audiod: the lock plugin is NOT enabled (agreed precedence)")

# C. locked, SBSOpenSensitiveURLAndUnlock missing: one clear log line (once per process), plugin first.
w, allev, log = scenario("unlock-missing", {"AUDIOD_MOCK_LOCKED": "1", "AUDIOD_MOCK_UNLOCK": "missing"}, [],
                         [(0, "spotify", 1200), (6200, "airplay", 1200)], 8200)
un = has(w, "wake: device locked, unlock unavailable: staying on the lock screen")
check(len(un) == 2 and count(log, UNLOCK_CALL) == 0, f"symbol missing: both starts stay on the lock screen, no unlock call ({len(un)} events)")
check(count(log, r"SBSOpenSensitiveURLAndUnlock is not available on this system") == 1, "symbol missing: the 'not available' line is logged once per process")
idx = [i for i, ev in enumerate(allev) if ev[1] == "wake" and (ev[4].startswith("lock-plugin enabled") or ev[4].startswith("wake: device locked"))]
order = [allev[i][4].split(":")[0] for i in idx]
check(order[:2] == ["lock-plugin enabled fidelio", "wake"], f"symbol missing: lock plugin enabled before the wake (B1.11 order): {order[:2]}")

# D. locked, unlock_on_play off: no unlock call, the device stays on the lock screen, plugin enabled.
w, _, log = scenario("unlock-off", {"AUDIOD_MOCK_LOCKED": "1", "AUDIOD_MOCK_UNLOCK": "ok"}, ["--unlock-on-play", "off"],
                     [(0, "spotify", 1500)], 3000)
check(len(has(w, "wake: device locked, unlock-on-play off: staying on the lock screen")) == 1, "setting off: locked -> stays on the lock screen")
check(count(log, UNLOCK_CALL) == 0 and count(log, WAKE_CALL) == 1, "setting off: no unlock call, only the wake's launch")
check(len(has(w, "lock-plugin enabled fidelio")) == 1, "setting off: the lock plugin is enabled")

# E. locked, the unlock request fails: logged once, never retried in a loop, plugin enabled.
w, _, log = scenario("unlock-refused", {"AUDIOD_MOCK_LOCKED": "1", "AUDIOD_MOCK_UNLOCK": "refuse"}, [], [(0, "spotify", 2500)], 3500)
check(len(has(w, "wake: device locked, unlock request failed (nowplaying:)")) == 1, "refused: one 'unlock request failed' event")
check(count(log, UNLOCK_CALL) == 1 and count(log, WAKE_CALL) == 1, f"refused: one unlock call and no retry loop ({count(log, UNLOCK_CALL)} calls)")
check(len(has(w, "lock-plugin enabled fidelio")) == 1, "refused: the lock plugin is enabled")

# F. B1.16.6 regression: SpringBoard accepts the request but would keep answering 3 to a launch (the
#    5.1.1 iPad does that for seconds although it IS unlocked). B1.16.4 retried the launch, gave up with
#    "unlock not confirmed" and enabled the plugin on an unlocked device. Also: a B1.16.4 command line
#    with --unlock-timeout-ms still starts (the flag is accepted and ignored, with a note).
w, _, log = scenario("unlock-stuck", {"AUDIOD_MOCK_LOCKED": "1", "AUDIOD_MOCK_UNLOCK": "stuck"}, ["--unlock-timeout-ms", "2000"],
                     [(0, "spotify", 3000)], 4000)
check(len(has(w, "wake: device locked, unlock accepted (nowplaying:)")) == 1 and not has(w, "unlock not confirmed"),
      "accepted but launches would still answer 3: the accepted request is the result (no false 'not confirmed')")
check(count(log, WAKE_CALL) == 1 and count(log, UNLOCK_CALL) == 1, f"accepted: no launch probe at all ({count(log, WAKE_CALL)} launch, {count(log, UNLOCK_CALL)} unlock call)")
check(not has(w, "lock-plugin enabled"), "accepted: the lock plugin stays off for the whole start")
check(count(log, r"note: --unlock-timeout-ms is ignored since B1\.16\.6") == 1 and len(w) >= 1,
      "--unlock-timeout-ms (B1.16.4) is still accepted: audiod starts, notes that it is ignored")

# G. B1.16.10 wake_mode=screen on a LOCKED device: only the screen is lit. Nothing is launched or
#    unlocked, and the lock plugin (the backstop) is enabled before the wake, so the lit lock screen
#    already shows the track.
w, allev, log = scenario("screen-only", {"AUDIOD_MOCK_LOCKED": "1", "AUDIOD_MOCK_UNLOCK": "ok"}, ["--wake-mode", "screen"],
                         [(0, "spotify", 1500)], 3000)
check(len(has(w, "wake (screen only)")) == 1 and count(log, r"mock waker: wakeScreen\(\)") == 1, "screen mode: one screen-only wake")
check(count(log, WAKE_CALL) == 0 and count(log, UNLOCK_CALL) == 0 and not [ev for ev in w if "unlock" in ev[4]],
      "screen mode: nothing launched and no unlock attempted, although the device is locked")
order = [ev[4] for ev in allev if ev[1] == "wake" and (ev[4].startswith("lock-plugin enabled") or ev[4] == "wake (screen only)")]
check(order[:2] == ["lock-plugin enabled fidelio", "wake (screen only)"], f"screen mode: the lock plugin is enabled before the screen wakes ({order[:2]})")

print("\n" + (f"{len(fails)} FAILURES" if fails else "all unlock-on-play checks passed"))
sys.exit(1 if fails else 0)
