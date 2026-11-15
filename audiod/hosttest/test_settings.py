#!/usr/bin/env python3
"""B1.14: the daemon writes a manifest from its registry, honours command line > settings.conf >
default, marks command-line options locked, and applies a settings.conf change live (updating the
manifest) without a restart. Usage: test_settings.py PATH_TO_fidelio-audiod
"""
import os, plistlib, subprocess, sys, time
AUDIOD = sys.argv[1]
TMP = "/tmp/audiod-settings"; os.makedirs(TMP, exist_ok=True)
SOCK, DIR, CONF = f"{TMP}/a.sock", TMP, f"{TMP}/settings.conf"
MANIFEST = f"{TMP}/settings-audiod.manifest.plist"
for f in (SOCK, CONF, MANIFEST):
    if os.path.exists(f): os.unlink(f)
fails = []
def check(ok, msg):
    print(("ok   " if ok else "FAIL ") + msg)
    if not ok: fails.append(msg)
def manifest():
    with open(MANIFEST, "rb") as f: return plistlib.load(f)
def opt(m, key):
    for o in m["options"]:
        if o["key"] == key: return o
    return None

# --max-volume 90 on the command line (locked); the rest default
ad = subprocess.Popen([AUDIOD, "--sock", SOCK, "--status-dir", DIR, "--no-audio", "--max-volume", "90"],
                      stdout=open(f"{TMP}/audiod.log", "w"), stderr=subprocess.STDOUT)
for _ in range(60):
    if os.path.exists(MANIFEST): break
    time.sleep(0.05)
check(os.path.exists(MANIFEST), "the daemon wrote settings-audiod.manifest.plist at startup")
m = manifest()
check(m.get("daemon") == "audiod" and m.get("version") == 1 and isinstance(m.get("options"), list), "manifest has version, daemon and the option list")
mv = opt(m, "max_volume")
check(mv and mv["value"] == "90" and mv["origin"] == "command line" and mv["locked"], f"--max-volume 90 is effective and locked ({mv['value'] if mv else None}, locked={mv['locked'] if mv else None})")
wk = opt(m, "wake_on_play")
check(wk and wk["value"] == "on" and wk["origin"] == "default" and not wk["locked"], "wake_on_play is the default and not locked")
groups = [o["group"] for o in m["options"]]
check("Volume" in groups and "Advanced" in groups, "options are grouped (Volume, Advanced, ...)")
adv = opt(m, "takeover_ms")
check(adv and adv["advanced"] and adv["type"] == "int" and adv.get("unit") == "ms", "arbiter timing is Advanced, typed int with a ms unit")

# live change via settings.conf: crossfade + wake change; the locked max_volume is ignored
with open(CONF, "w") as f:
    f.write("audiod.crossfade_ms=60\naudiod.max_volume=50\naudiod.wake_on_play=off\naudiod.bogus=1\nnot a line\n")
time.sleep(1.8)  # > the 1 s reload check + a tick
m = manifest()
check(opt(m, "crossfade_ms")["value"] == "60" and opt(m, "crossfade_ms")["origin"] == "file", "a live option changed via settings.conf without a restart (crossfade_ms=60, origin file)")
check(opt(m, "wake_on_play")["value"] == "off" and opt(m, "wake_on_play")["origin"] == "file", "wake_on_play changed to off via settings.conf")
check(opt(m, "max_volume")["value"] == "90", "the command-line-locked max_volume ignored the settings.conf value")
log = open(f"{TMP}/audiod.log").read()
check("settings.conf changed: applied live" in log, "the daemon logged the live apply")

ad.terminate(); ad.wait()
print("\n" + (f"{len(fails)} FAILURES" if fails else "all settings-daemon checks passed"))
sys.exit(1 if fails else 0)
