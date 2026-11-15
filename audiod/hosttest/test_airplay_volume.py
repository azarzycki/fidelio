#!/usr/bin/env python3
"""B1.13: an AirPlay sender's volume drives the device hardware level (absolute mode). The opening
volume is not applied (no surprise jump); -144 never zeroes; the --max-volume ceiling holds; the
device level is pushed to the sender over DACP at session start and after a change. A sender without
DACP gets no push. Usage: test_airplay_volume.py AUDIOD RAOP
"""
import os, subprocess, sys, threading, time
AUDIOD, RAOP = sys.argv[1], sys.argv[2]
HERE = os.path.dirname(os.path.abspath(__file__))
SENDER = os.path.join(HERE, "..", "..", "airplay", "hosttest", "raop_sender.py")
TMP = "/tmp/audiod-airvol"; os.makedirs(TMP, exist_ok=True)
fails = []
def check(ok, msg):
    print(("ok   " if ok else "FAIL ") + msg)
    if not ok: fails.append(msg)

def run(tag, mock_hw="0.30", max_vol="80", seq="-15,-3,-144", opening="-5", dacp_port=6400, no_dacp=False, change=None):
    base = f"{TMP}/{tag}"; sock, out, evt = f"{base}.sock", f"{base}.pcm", f"{base}.ev"
    chg = f"{base}.chg"
    for f in (sock, out, evt, chg):
        if os.path.exists(f): os.unlink(f)
    aenv = dict(os.environ)
    aargs = [AUDIOD, "--sock", sock, "--pcm-out", out, "--event-log", evt, "--no-status", "--no-silence",
             "--airplay-volume-mode", "absolute", "--max-volume", max_vol, "--wake-on-play", "off",
             "--takeover-ms", "400", "--hold-ms", "500", "--release-ms", "1500", "--lead-ms", "750"]
    if mock_hw is not None:
        aenv["AUDIOD_MOCK_HW_VOLUME"] = mock_hw; aenv["AUDIOD_MOCK_HW_CHANGE_FILE"] = chg
    ad = subprocess.Popen(aargs, stdout=open(f"{base}-audiod.log","w"), stderr=subprocess.STDOUT, env=aenv)
    for _ in range(50):
        if os.path.exists(sock): break
        time.sleep(0.05)
    renv = dict(os.environ, AUDIOD_RAOP_DACP_ADDR=f"127.0.0.1:{dacp_port}")
    rp = subprocess.Popen([RAOP, "--audiod-sock", sock, "--port", str(5400 + dacp_port % 50), "--no-status",
                           "--name", "hosttest", "--latency-ms", "500"],
                          stdout=open(f"{base}-raop.log","w"), stderr=subprocess.STDOUT, env=renv)
    time.sleep(0.8)
    sargs = [sys.executable, SENDER, "--port", str(5400 + dacp_port % 50), "--codec", "pcm", "--volume-test",
             f"--volume={opening}", f"--volume-seq={seq}", "--dacp-listen", str(dacp_port), "--stream-secs", "6"]
    if no_dacp: sargs.append("--no-dacp")
    snd = subprocess.Popen(sargs, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    if change:
        def butt():
            time.sleep(change[0]); open(chg, "w").write(f"{change[1]}\n")
        threading.Thread(target=butt, daemon=True).start()
    txt, _ = snd.communicate(timeout=20)
    res = {}
    if "VOLTEST" in txt:
        for tok in txt.split("VOLTEST",1)[1].split("\n")[0].split():
            k,_,v = tok.partition("="); res[k]=v
    raoplog = open(f"{base}-raop.log").read()
    ev = [l.split() for l in open(evt)] if os.path.exists(evt) else []
    rp.terminate(); rp.wait(); ad.terminate(); ad.wait()
    return res, raoplog, ev

# absolute: opening -5 not applied; -15 sets 0.50; -3 clamps to 0.80; -144 no set/zero
res, log, ev = run("abs", change=(3.5, "0.60"))
sets = [e for e in ev if len(e)>3 and e[1]=="hwvolume" and e[2]=="airplay"]
setvals = [e[3] for e in sets]
check("session-opening value, NOT applied" in log, "opening -5 dB was not applied (no surprise jump)")
check("0.500" in setvals, f"a real -15 dB set the hardware to level 0.50 ({setvals})")
check(any(s=="0.800" for s in setvals) and "clamped" in " ".join(" ".join(e) for e in sets), f"-3 dB was clamped to the --max-volume 0.80 ceiling ({setvals})")
check("sentinel (no remote volume) -> unity" in log and "0.000" not in setvals, "-144 dB played at unity and never zeroed the hardware")
devvols = res.get("devvols","").split(";") if res.get("devvols") else []
check(any(abs(float(v) - (-21.0)) < 1.0 for v in devvols if v), f"DACP setproperty pushed the session-start level ~-21 dB (0.30) to the sender ({devvols})")
check(any(abs(float(v) - (-12.0)) < 2.0 for v in devvols if v), f"DACP setproperty pushed the button change (0.60 -> ~-12 dB) to the sender ({devvols})")

# a sender with no DACP: the hardware is still set, but no setproperty is pushed
res, log, ev = run("nodacp", no_dacp=True, dacp_port=6401, seq="-15")
check(not (res.get("devvols")), f"a sender without DACP receives no setproperty ({res.get('devvols')})")
check(any(e[3]=="0.500" for e in ev if len(e)>3 and e[1]=="hwvolume"), "the hardware is still set from the sender's volume without DACP")

# -144 at session start must never zero the hardware (CenturyPlay)
res, log, ev = run("mute_start", opening="-144", seq="-144", dacp_port=6402)
check(not any(len(e)>3 and e[1]=="hwvolume" and e[2]=="airplay" and e[3]=="0.000" for e in ev), "a session that opens with -144 dB never sets the hardware to zero")

# software fallback: no hardware backend -> software gain (B1.4), no hardware set at all
res, log, ev = run("soft", mock_hw=None, dacp_port=6403, seq="-15")
check("software fallback" in open(f"{TMP}/soft-audiod.log").read() or "airplay volume: software" in open(f"{TMP}/soft-audiod.log").read(), "no hardware backend -> audiod logs the software fallback")
check("software gain" in log and not any(len(e)>3 and e[1]=="hwvolume" and e[2]=="airplay" for e in ev), "software fallback: AirPlay uses a gain, the hardware is never touched")

print("\n" + (f"{len(fails)} FAILURES" if fails else "all airplay-volume checks passed"))
sys.exit(1 if fails else 0)
