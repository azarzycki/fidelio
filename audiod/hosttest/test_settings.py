#!/usr/bin/env python3
"""B1.14/B1.15: each daemon declares its options once, writes a manifest whose effective value is read
back from the live config, honours command line > settings.conf > default, marks every flag spelling
(short and long) locked, and really APPLIES a file value (asserted via the daemon's own start log / a
self-check line, not just the manifest text). audiod also applies a change live.

Usage: test_settings.py PATH_TO_fidelio-audiod [PATH_TO_fidelio-raop PATH_TO_cspot-ios3]
"""
import os, plistlib, signal, subprocess, sys, time

AUDIOD = sys.argv[1]
RAOP = sys.argv[2] if len(sys.argv) > 2 else None
CSPOT = sys.argv[3] if len(sys.argv) > 3 else None
TMP = "/tmp/audiod-settings"; os.makedirs(TMP, exist_ok=True)
fails = []
def check(ok, msg):
    print(("ok   " if ok else "FAIL ") + msg)
    if not ok: fails.append(msg)
def manifest(path):
    with open(path, "rb") as f: return plistlib.load(f)
def opt(m, key):
    for o in m["options"]:
        if o["key"] == key: return o
    return None
def wait_file(p, t=5.0):
    end = time.time() + t
    while time.time() < end:
        if os.path.exists(p): return True
        time.sleep(0.05)
    return False
def clean(*paths):
    for p in paths:
        try: os.unlink(p)
        except OSError: pass

# ---------------------------------------------------------------- audiod ----------------------------------
SOCK, CONF = f"{TMP}/a.sock", f"{TMP}/settings.conf"
MAN = f"{TMP}/settings-audiod.manifest.plist"
LOCK = f"{TMP}/lockplugin.plist"
clean(SOCK, CONF, MAN, LOCK)
ad = subprocess.Popen([AUDIOD, "--sock", SOCK, "--status-dir", TMP, "--no-audio", "--max-volume", "90", "--keep-screen-on", "on"],
                      stdout=open(f"{TMP}/audiod.log", "w"), stderr=subprocess.STDOUT)
check(wait_file(MAN), "audiod wrote settings-audiod.manifest.plist at startup")
m = manifest(MAN)
check(m.get("daemon") == "audiod" and m.get("version") == 1 and isinstance(m.get("options"), list), "audiod manifest has version, daemon and the option list")
mv = opt(m, "max_volume")
check(mv and mv["value"] == "90" and mv["origin"] == "command line" and mv["locked"], "audiod --max-volume 90 is effective and locked")
wk = opt(m, "wake_on_play")
check(wk and wk["value"] == "on" and wk["origin"] == "default" and not wk["locked"], "audiod wake_on_play is the default and not locked")
ul = opt(m, "unlock_on_play")
check(ul and ul["value"] == "on" and ul["origin"] == "default" and ul["group"] == "Display", "audiod unlock_on_play is in the manifest (Display), default on (B1.16.4)")
ks = opt(m, "keep_screen_on")
check(ks and ks["value"] == "on" and ks["group"] == "Display", "audiod keep_screen_on is in the manifest (Display), value on")
check(wait_file(LOCK) and manifest(LOCK).get("KeepScreenOn") is True, "audiod wrote lockplugin.plist with KeepScreenOn=true (single owner)")
alog = open(f"{TMP}/audiod.log").read()
check("self-check ok" in alog, "audiod logged the settings self-check ok")

# live change via settings.conf: crossfade changes, the locked max_volume is ignored
with open(CONF, "w") as f:
    f.write("audiod.crossfade_ms=60\naudiod.max_volume=50\naudiod.wake_on_play=off\naudiod.unlock_on_play=off\naudiod.bogus=1\nnot a line\n")
time.sleep(1.8)
m = manifest(MAN)
check(opt(m, "crossfade_ms")["value"] == "60" and opt(m, "crossfade_ms")["origin"] == "file", "audiod applied a live settings.conf change (crossfade_ms=60, origin file)")
check(opt(m, "wake_on_play")["value"] == "off" and opt(m, "wake_on_play")["origin"] == "file", "audiod wake_on_play changed to off via settings.conf")
check(opt(m, "unlock_on_play")["value"] == "off" and opt(m, "unlock_on_play")["origin"] == "file", "audiod unlock_on_play changed to off via settings.conf (live)")
check(opt(m, "max_volume")["value"] == "90", "audiod command-line-locked max_volume ignored the settings.conf value")
check("settings.conf changed: applied live" in open(f"{TMP}/audiod.log").read(), "audiod logged the live apply")
ad.terminate(); ad.wait()

# ---------------------------------------------------------------- cspot -----------------------------------
def run_briefly(argv, logpath, manpath):
    clean(manpath)
    p = subprocess.Popen(argv, stdout=open(logpath, "w"), stderr=subprocess.STDOUT)
    ok = wait_file(manpath)
    time.sleep(0.3)  # let the start line print
    p.send_signal(signal.SIGINT);
    try: p.wait(timeout=3)
    except subprocess.TimeoutExpired: p.kill(); p.wait()
    return ok

if CSPOT:
    D = f"{TMP}/cs"; os.makedirs(D, exist_ok=True)
    MANC, CONFC, LOGC = f"{D}/settings-cspot.manifest.plist", f"{D}/settings.conf", f"{D}/cspot.log"
    # (a) SHORT flags -b 320 -n "iPad Fidelio"
    clean(CONFC)
    run_briefly([CSPOT, "--no-audio", "--port", "0", "--status-dir", D, "-b", "320", "-n", "iPad Fidelio"], LOGC, MANC)
    m = manifest(MANC)
    bo, no = opt(m, "bitrate"), opt(m, "name")
    check(bo and bo["value"] == "320" and bo["origin"] == "command line" and bo["locked"], "cspot -b 320 (short) is effective and locked in the manifest")
    check(no and no["value"] == "iPad Fidelio" and no["origin"] == "command line" and no["locked"] and no["type"] == "string", "cspot -n (short, STRING) is effective and locked")
    log = open(LOGC).read()
    check("bitrate=320" in log and "name='iPad Fidelio'" in log, "cspot start log reflects the effective bitrate 320 and name (the code really uses them)")
    check("self-check ok" in log, "cspot self-check ok (manifest matches the live config)")
    # (b) FILE value cspot.bitrate=320, no -b: must REACH the player (defect 3), start log shows 320
    with open(CONFC, "w") as f: f.write("cspot.bitrate=320\n")
    run_briefly([CSPOT, "--no-audio", "--port", "0", "--status-dir", D], LOGC, MANC)
    m = manifest(MANC); bo = opt(m, "bitrate")
    check(bo and bo["value"] == "320" and bo["origin"] == "file", "cspot settings.conf bitrate=320 is shown as origin file")
    check("bitrate=320" in open(LOGC).read(), "cspot file bitrate=320 really reached the start/player path (not just the manifest)")
    # (c) DEFAULTS match the program's own (defect 4): bitrate 96, name 'iPhone Fidelio'
    clean(CONFC)
    run_briefly([CSPOT, "--no-audio", "--port", "0", "--status-dir", D], LOGC, MANC)
    m = manifest(MANC)
    check(opt(m, "bitrate")["value"] == "96" and opt(m, "bitrate")["origin"] == "default", "cspot default bitrate is 96 (matches --help / the struct)")
    check(opt(m, "name")["value"] == "iPhone Fidelio" and opt(m, "name")["origin"] == "default", "cspot default name is 'iPhone Fidelio' (matches the program)")
    check("self-check ok" in open(LOGC).read(), "cspot self-check ok on defaults (no drift)")

# ---------------------------------------------------------------- raop ------------------------------------
if RAOP:
    D = f"{TMP}/rp"; os.makedirs(D, exist_ok=True)
    MANR, CONFR, LOGR = f"{D}/settings-raop.manifest.plist", f"{D}/settings.conf", f"{D}/raop.log"
    # (a) SHORT -n sets a real name (was impossible while name was a fixed enum)
    clean(CONFR)
    run_briefly([RAOP, "--no-audio", "--port", "0", "--status-dir", D, "-n", "iPad Fidelio (AirPlay)"], LOGR, MANR)
    m = manifest(MANR); no = opt(m, "name")
    check(no and no["value"] == "iPad Fidelio (AirPlay)" and no["origin"] == "command line" and no["locked"] and no["type"] == "string", "raop -n (short, STRING) real name is effective and locked")
    check("self-check ok" in open(LOGR).read(), "raop self-check ok")
    # (b) FILE value raop.name=Bedroom applied
    with open(CONFR, "w") as f: f.write("raop.name=Bedroom\n")
    run_briefly([RAOP, "--no-audio", "--port", "0", "--status-dir", D], LOGR, MANR)
    m = manifest(MANR)
    check(opt(m, "name")["value"] == "Bedroom" and opt(m, "name")["origin"] == "file", "raop settings.conf name=Bedroom is applied (origin file)")
    # (c) DEFAULTS match
    clean(CONFR)
    run_briefly([RAOP, "--no-audio", "--port", "0", "--status-dir", D], LOGR, MANR)
    m = manifest(MANR)
    check(opt(m, "name")["value"] == "Fidelio (AirPlay)" and opt(m, "name")["origin"] == "default", "raop default name is 'Fidelio (AirPlay)' (matches the program)")
    check(opt(m, "airplay_preempt")["value"] == "pause-then-disconnect" and opt(m, "airplay_preempt")["origin"] == "default", "raop default airplay_preempt matches the program")

print("\n" + (f"{len(fails)} FAILURES" if fails else "all settings-daemon checks passed"))
sys.exit(1 if fails else 0)
