#!/usr/bin/env python3
"""Host test of B1.16.11: the lock plugin follows the live setting and survives a SpringBoard restart.

onStartFromNone decides about the plugin when playback starts (B1.11, B1.16.4, B1.16.10). While a source
keeps playing, audiod now also:
  - applies a live lock_plugin change at once: on -> enabled, off -> disabled. Not after audiod
    unlocked the device for that start (the agreed rule);
  - re-enables the plugin after a SpringBoard restart. A respring forgets an enabled bundle, and the
    device showed no plugin until the next start from nothing. SpringBoard's pid is polled every 2 s and
    the bundle is enabled again once a new SpringBoard has been up for 4 s.
The host build's mock waker logs enableLockBundle/disableLockBundle and fakes the restart: SpringBoard's
pid changes AUDIOD_MOCK_SB_RESTART_MS after start. Usage: test_lockplugin.py PATH_TO_dock-audiod
"""
import os, re, socket, struct, subprocess, sys, threading, time

AUDIOD = sys.argv[1]
TAKEOVER, HOLD, RELEASE, IDLE_STOP, LEAD = 400, 1500, 1000, 2000, 750
TMP = "/tmp/audiod-lockplugin"
os.makedirs(TMP, exist_ok=True)
fails = []


def check(ok, msg):
    print(("ok   " if ok else "FAIL ") + msg)
    if not ok:
        fails.append(msg)


def hello(name):
    return struct.pack("<BBBBI32s", 1, 0, 2, 16, 44100, name.encode())


def stream(sock, name, ms):
    """connect and stream `ms` of loud audio in real time (plus the LEAD burst) on a thread"""
    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    for attempt in range(50):
        try:
            s.connect(sock)
            break
        except (ConnectionRefusedError, FileNotFoundError):
            if attempt == 49:
                raise
            time.sleep(0.05)
    s.sendall(struct.pack("<II", 1, 40) + hello(name))

    def run():
        chunk = struct.pack("<hh", 0x2222, 0x2222) * 882
        t0 = time.monotonic()
        for i in range((ms + LEAD) // 20):
            d = (i * 20 - LEAD) / 1000 - (time.monotonic() - t0)
            if d > 0:
                time.sleep(d)
            try:
                s.sendall(struct.pack("<II", 2, len(chunk)) + chunk)
            except OSError:
                break
    t = threading.Thread(target=run, daemon=True)
    t.start()
    return s, t


def scenario(name, env, args, conf_steps, play_ms, total_ms):
    """conf_steps: [(at_ms, settings.conf text)]; the first (at 0) is written before audiod starts.
    Returns (wake events [(ms, reason)], audiod log text)."""
    d = f"{TMP}/{name}"
    os.makedirs(d, exist_ok=True)
    sock, out, evt, log, conf = f"{d}/audiod.sock", f"{d}/out.pcm", f"{d}/events.log", f"{d}/audiod.log", f"{d}/settings.conf"
    for f in (sock, out, evt, conf):
        if os.path.exists(f):
            os.unlink(f)
    steps = list(conf_steps)
    if steps and steps[0][0] == 0:
        with open(conf, "w") as f:
            f.write(steps.pop(0)[1])
        old = time.time() - 10
        os.utime(conf, (old, old))  # an older mtime, so the first live rewrite is always seen as a change
    e = dict(os.environ)
    for k in ("AUDIOD_MOCK_LOCKED", "AUDIOD_MOCK_UNLOCK", "AUDIOD_MOCK_SB_RESTART_MS"):
        e.pop(k, None)
    e.update(env)
    proc = subprocess.Popen([AUDIOD, "--sock", sock, "--pcm-out", out, "--event-log", evt, "--status-dir", d, "--no-silence",
                             "--takeover-ms", str(TAKEOVER), "--hold-ms", str(HOLD), "--release-ms", str(RELEASE),
                             "--idle-stop-ms", str(IDLE_STOP), "--lead-ms", str(LEAD), "--wake-on-play", "on",
                             "--wake-bundle", "com.dockreceiver.nowplaying"] + args,
                            stdout=open(log, "w"), stderr=subprocess.STDOUT, env=e)
    for _ in range(50):
        if os.path.exists(sock):
            break
        time.sleep(0.05)
    t0 = time.monotonic()
    s, t = stream(sock, "spotify", play_ms)
    for at, text in steps:
        dly = at / 1000 - (time.monotonic() - t0)
        if dly > 0:
            time.sleep(dly)
        with open(conf, "w") as f:
            f.write(text)
    dly = total_ms / 1000 - (time.monotonic() - t0)
    if dly > 0:
        time.sleep(dly)
    t.join()
    s.close()
    proc.terminate()
    proc.wait()
    wakes = []
    for line in open(evt):
        tm, k, frm, to, *reason = line.split()
        if k == "wake":
            wakes.append((int(tm), " ".join(reason)))
    text = open(log).read()
    print(f"-- {name}: " + "; ".join(f"{w[0]} {w[1]}" for w in wakes))
    return wakes, text


def has(w, prefix):
    return [x for x in w if x[1].startswith(prefix)]


def count(text, rx):
    return len(re.findall(rx, text, re.M))


# A. The setting turned on, then off, while a source plays. No SpringBoard restart, so no re-enable.
w, log = scenario("live-toggle", {}, [], [(0, "audiod.lock_plugin=off\n"), (2500, "audiod.lock_plugin=on\n"), (6000, "audiod.lock_plugin=off\n")],
                  9000, 9500)
on = has(w, "lock-plugin enabled NowPlaying (turned on while playing)")
off = has(w, "lock-plugin disabled NowPlaying (turned off while playing)")
check(len(on) == 1 and 2500 <= on[0][0] <= 5000, f"turned on while playing: enabled at once ({on})")
check(len(off) == 1 and on and off[0][0] > on[0][0] and 6000 <= off[0][0] <= 8500, f"turned off while playing: disabled at once ({off})")
check(count(log, r"mock waker: enableLockBundle\(NowPlaying\)") == 1 and count(log, r"mock waker: disableLockBundle\(NowPlaying\)") == 1,
      "exactly one enable and one disable reached SpringBoard, under the enabled name")
check(not has(w, "lock-plugin re-enabled"), "no SpringBoard restart: no re-enable")

# B. SpringBoard restarts 3 s into playback: the plugin is enabled again, once, after the new one settles.
w, log = scenario("sb-restart", {"AUDIOD_MOCK_SB_RESTART_MS": "3000"}, ["--lock-plugin", "NowPlaying"], [], 11500, 12000)
re_en = has(w, "lock-plugin re-enabled NowPlaying after a SpringBoard restart")
check(len(has(w, "lock-plugin enabled NowPlaying")) == 1, "the plugin is enabled at the start from nothing")
check(len(re_en) == 1 and 6000 <= re_en[0][0] <= 10500, f"a SpringBoard restart while playing re-enables it once, after the new one settles ({re_en})")
check(count(log, r"mock waker: enableLockBundle\(NowPlaying\)") == 2, f"two enable calls in all: the start and the re-enable ({count(log, r'mock waker: enableLockBundle')})")

# C. The agreed rule: audiod unlocked the device for this start, so turning the setting on keeps it off.
w, log = scenario("unlocked-start", {"AUDIOD_MOCK_LOCKED": "1", "AUDIOD_MOCK_UNLOCK": "ok"}, [],
                  [(0, "audiod.lock_plugin=off\n"), (2500, "audiod.lock_plugin=on\n")], 6000, 6500)
check(len(has(w, "wake: device locked, unlock accepted")) == 1 and not has(w, "lock-plugin enabled") and count(log, r"mock waker: enableLockBundle") == 0,
      "after an audiod unlock, turning the setting on mid-playback does not enable the plugin (agreed rule)")

print("\n" + (f"{len(fails)} FAILURES" if fails else "all lock-plugin sync checks passed"))
sys.exit(1 if fails else 0)
