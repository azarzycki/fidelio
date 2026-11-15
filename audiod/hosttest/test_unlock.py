#!/usr/bin/env python3
"""Host test of B1.16.4 unlock-on-play and its precedence over the lock plugin.

When playback starts from nothing, audiod wakes the display and launches NowPlaying (B1.9). If that
launch is refused because the device is locked (SpringBoard code 3), audiod asks SpringBoard to unlock
and open the app's URL (SBSOpenSensitiveURLAndUnlock) and retries the launch every 500 ms until it
succeeds or --unlock-timeout-ms pass. The lock plugin (B1.11) is enabled only when audiod does not
unlock the device for that start. The host build's mock waker plays a locked device (env
AUDIOD_MOCK_LOCKED / AUDIOD_MOCK_UNLOCK / AUDIOD_MOCK_UNLOCK_MS) and logs every call
("mock waker: ..."); audiod logs every decision to the event log (kind "wake").
Each scenario runs its own audiod. Usage: test_unlock.py PATH_TO_fidelio-audiod
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
    for k in ("AUDIOD_MOCK_LOCKED", "AUDIOD_MOCK_UNLOCK", "AUDIOD_MOCK_UNLOCK_MS"):
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
LAUNCH_CALL = r"mock waker: launch\("

# A. not locked: the launch succeeds, no unlock is attempted, the lock plugin is enabled as before.
w, _, log = scenario("unlocked", {}, [], [(0, "spotify", 1500)], 3000)
check(len(has(w, "wake+launch com.fidelio.nowplaying")) == 1, "unlocked: the wake's launch succeeded (wake+launch)")
check(not [ev for ev in w if "unlock" in ev[4]] and count(log, UNLOCK_CALL) == 0, "unlocked: no unlock requested (launch 0 -> no unlock call)")
lp = has(w, "lock-plugin enabled fidelio")
check(len(lp) == 1, "unlocked: the lock plugin is enabled (audiod did not unlock the device)")

# B. locked, the unlock works (the mock unlocks 1.5 s after the request): retried launch reaches 0.
w, _, log = scenario("unlock-ok", {"AUDIOD_MOCK_LOCKED": "1", "AUDIOD_MOCK_UNLOCK": "ok", "AUDIOD_MOCK_UNLOCK_MS": "1500"}, [],
                     [(0, "spotify", 4000)], 5000)
req = has(w, "wake: device locked, unlock requested (nowplaying:)")
check(len(req) == 1, "locked: launch 3 -> unlock requested with the app's URL nowplaying:")
check(count(log, UNLOCK_CALL + r"nowplaying:\) -> 1") == 1 and count(log, UNLOCK_CALL) == 1, "locked: exactly one SBSOpenSensitiveURLAndUnlock call")
ok = has(w, "unlocked: launch ok after")
m = re.search(r"after (\d+) ms \((\d+) launch", ok[0][4]) if ok else None
check(m is not None and 1500 <= int(m.group(1)) <= 2100, f"locked: the retried launch reached 0 once the device unlocked ({ok[0][4] if ok else 'no unlocked event'})")
check(m is not None and int(m.group(2)) == count(log, LAUNCH_CALL) and 3 <= int(m.group(2)) <= 5,
      f"locked: launch retried every 500 ms until it succeeded ({count(log, LAUNCH_CALL)} retries)")
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
check(count(log, UNLOCK_CALL) == 0 and count(log, LAUNCH_CALL) == 0, "setting off: no unlock call, no launch retry")
check(len(has(w, "lock-plugin enabled fidelio")) == 1, "setting off: the lock plugin is enabled")

# E. locked, the unlock request fails: logged once, never retried in a loop, plugin enabled.
w, _, log = scenario("unlock-refused", {"AUDIOD_MOCK_LOCKED": "1", "AUDIOD_MOCK_UNLOCK": "refuse"}, [], [(0, "spotify", 2500)], 3500)
check(len(has(w, "wake: device locked, unlock request failed (nowplaying:)")) == 1, "refused: one 'unlock request failed' event")
check(count(log, UNLOCK_CALL) == 1 and count(log, LAUNCH_CALL) == 0, f"refused: one unlock call and no retry loop ({count(log, UNLOCK_CALL)} calls)")
check(len(has(w, "lock-plugin enabled fidelio")) == 1, "refused: the lock plugin is enabled")

# F. locked, the request is delivered but the device never unlocks: give up after --unlock-timeout-ms.
w, allev, log = scenario("unlock-stuck", {"AUDIOD_MOCK_LOCKED": "1", "AUDIOD_MOCK_UNLOCK": "stuck"}, ["--unlock-timeout-ms", "2000"],
                         [(0, "spotify", 4000)], 4500)
nc = has(w, "unlock not confirmed after")
m = re.search(r"after (\d+) ms \(launch 3, (\d+) launch", nc[0][4]) if nc else None
check(m is not None and 2000 <= int(m.group(1)) <= 2600, f"stuck: gave up after the timeout ({nc[0][4] if nc else 'no event'})")
check(m is not None and int(m.group(2)) == count(log, LAUNCH_CALL) and 3 <= int(m.group(2)) <= 5,
      f"stuck: bounded retries, no loop past the timeout ({count(log, LAUNCH_CALL)} launches)")
lp = has(w, "lock-plugin enabled fidelio")
check(len(lp) == 1 and nc and lp[0][0] >= nc[0][0] and count(log, UNLOCK_CALL) == 1,
      "stuck: still locked -> the lock plugin is enabled after giving up, not before")

# G. locked, playback stops while audiod waits for the unlock: the wait is cancelled, no plugin.
w, _, log = scenario("unlock-cancel", {"AUDIOD_MOCK_LOCKED": "1", "AUDIOD_MOCK_UNLOCK": "ok", "AUDIOD_MOCK_UNLOCK_MS": "4000"}, [],
                     [(0, "spotify", 600)], 3500)
check(len(has(w, "unlock wait cancelled: nothing is playing any more")) == 1, "stopped while waiting: the unlock wait is cancelled")
check(not has(w, "unlocked:") and not has(w, "unlock not confirmed") and not has(w, "lock-plugin enabled"),
      "stopped while waiting: no unlock outcome and no lock plugin afterwards")

print("\n" + (f"{len(fails)} FAILURES" if fails else "all unlock-on-play checks passed"))
sys.exit(1 if fails else 0)
