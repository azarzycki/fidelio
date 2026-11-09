#!/usr/bin/env python3
"""Host test of the real producers feeding fidelio-audiod through SocketSink.

  test_producers.py AUDIOD RAOP [CSPOT]

1. fidelio-raop (host build, default output = audiod) receives a scripted RAOP session from
   airplay/hosttest/raop_sender.py (L16, no encryption: N packets, FLUSH, 150 more, TEARDOWN).
   audiod's --pcm-out must contain the sent PCM, scaled by the sender's -15 dB volume (SocketSink
   applies the producer's volume as gain): a contiguous exact run that may start up to ~1 s in
   (the arbiter's 400 ms takeover window plus the 500 ms lead are the designed cost of a source
   starting from silence), then after the FLUSH an exact continuation from packet N. The event
   log must show airplay taking over once and being released by the TEARDOWN's STOP.
   raop runs with --latency-ms 500 here: with the default 1200 ms cushion the re-prime after a
   FLUSH is longer than audiod's RELEASE_MS (1000), so audiod releases the source and takes it
   over again 400 ms into the new audio (see STATUS.md: a tuning choice for the device).
1b. A second RAOP session whose sender volume is -144 dB (the AirPlay mute sentinel that senders
   without remote volume, e.g. CenturyPlay, send permanently) must play at UNITY through audiod
   (B1.4 C): its packets appear unscaled, byte-exact, in audiod's output.
2. (optional) cspot-ios3 --sink-test 2: two seconds of a 440 Hz tone through its audiod sink;
   audiod's output must grow by ~2 s of tone (frame count and zero crossings), and the event log
   must show spotify taking over and being released by STOP.
"""
import os, struct, subprocess, sys, threading, time

AUDIOD, RAOP = sys.argv[1], sys.argv[2]
CSPOT = sys.argv[3] if len(sys.argv) > 3 else None
HERE = os.path.dirname(os.path.abspath(__file__))
SENDER = os.path.join(HERE, "..", "..", "airplay", "hosttest", "raop_sender.py")
TMP = "/tmp/audiod-producers"
os.makedirs(TMP, exist_ok=True)
SOCK, OUT, EVT = f"{TMP}/audiod.sock", f"{TMP}/out.pcm", f"{TMP}/events.log"
for f in (SOCK, OUT, EVT):
    if os.path.exists(f): os.unlink(f)
PK = 352 * 4
N = 400  # packets before the FLUSH
fails = []

def check(ok, msg):
    print(("ok   " if ok else "FAIL ") + msg)
    if not ok: fails.append(msg)

def events():
    ev = []
    for line in open(EVT):
        t, kind, frm, to, *reason = line.split()
        ev.append((int(t), kind, frm, to, " ".join(reason)))
    return ev

audiod = subprocess.Popen([AUDIOD, "--sock", SOCK, "--pcm-out", OUT, "--event-log", EVT, "--no-status", "--no-silence"],
                          stdout=open(f"{TMP}/audiod.log", "w"), stderr=subprocess.STDOUT)
for _ in range(50):
    if os.path.exists(SOCK): break
    time.sleep(0.05)

# ---------------------------------------------------------------- 1. fidelio-raop -> audiod
raop = subprocess.Popen([RAOP, "--audiod-sock", SOCK, "--port", "5100", "--no-status", "--name", "hosttest", "--latency-ms", "500"],
                        stdout=open(f"{TMP}/raop.log", "w"), stderr=subprocess.STDOUT)
time.sleep(0.5)
t0 = time.time()
snd = subprocess.run([sys.executable, SENDER, "--port", "5100", "--codec", "pcm", "--packets", str(N),
                      "--expect-pcm", f"{TMP}/expect.pcm"], capture_output=True, text=True)
open(f"{TMP}/sender.log", "w").write(snd.stdout + snd.stderr)
check(snd.returncode == 0, f"raop_sender.py finished all protocol checks (exit {snd.returncode}, {time.time() - t0:.1f} s)")
time.sleep(2.0)  # lead + audiod release
raop.terminate(); raop.wait()
time.sleep(0.3)

out = open(OUT, "rb").read()
exp = open(f"{TMP}/expect.pcm", "rb").read()
# SocketSink gain, emulated exactly: float32 volume, float32 product, C truncation toward zero.
g = struct.unpack("<f", struct.pack("<f", 10 ** (-15.0 / 20)))[0]
samples = struct.unpack("<%dh" % (len(exp) // 2), exp)
scaled = struct.pack("<%dh" % len(samples), *[int(struct.unpack("<f", struct.pack("<f", s * g))[0]) for s in samples])
# Pre-FLUSH: the output starts somewhere inside the first ~1 s of the stream (takeover window + lead;
# not on a packet boundary, since the lead is trimmed by frames) and must then be an exact, contiguous
# run of the scaled PCM up to within ~2 cushions of N (raop drops its jitter buffer and audiod the
# source's ring at FLUSH). After that, the rest of the output must be an exact prefix of the
# post-FLUSH packets (raop keeps the last few packets of a stream unplayed when an underrun precedes
# the TEARDOWN; that is its re-prime rule, not the socket path).
start = scaled.find(out[:PK]) if len(out) >= PK else -1
i = 0
if start >= 0:
    while i < len(out) and start + i < len(scaled) and out[i] == scaled[start + i]: i += 1
pre_start = start / PK if start >= 0 else -1
pre_end = (start + i) / PK if start >= 0 else -1
rest = out[i:]; post = scaled[N * PK:]
j = 0
while j < min(len(rest), len(post)) and rest[j] == post[j]: j += 1
check(0 <= pre_start <= 120 and pre_end >= N - 220, f"pre-FLUSH audio played through audiod, exact at -15 dB: packets {pre_start:.2f}..{pre_end:.2f} of {N}")
check(j == len(rest) and len(rest) // PK >= 20, f"post-FLUSH audio continues exactly from packet {N}: {len(rest) / PK:.2f} packets, {j / PK:.2f} matching")
raop_frames = len(out) // 4
ev = events()
tk = [e for e in ev if e[1] == "takeover" and e[3] == "airplay"]
rl = [e for e in ev if e[1] == "release" and e[2] == "airplay"]
check(len(tk) == 1, f"audiod: one takeover by airplay ({tk})")

# 1b. the -144 dB sentinel plays at unity
raop = subprocess.Popen([RAOP, "--audiod-sock", SOCK, "--port", "5101", "--no-status", "--name", "hosttest2", "--latency-ms", "500"],
                        stdout=open(f"{TMP}/raop2.log", "w"), stderr=subprocess.STDOUT)
time.sleep(0.5)
snd = subprocess.run([sys.executable, SENDER, "--port", "5101", "--codec", "pcm", "--packets", "150", "--volume", "-144",
                      "--expect-pcm", f"{TMP}/expect2.pcm"], capture_output=True, text=True)
open(f"{TMP}/sender2.log", "w").write(snd.stdout + snd.stderr)
time.sleep(2.0)
raop.terminate(); raop.wait()
time.sleep(0.3)
out2 = open(OUT, "rb").read()[raop_frames * 4:]
exp2 = open(f"{TMP}/expect2.pcm", "rb").read()
st2 = exp2.find(out2[:PK]) if len(out2) >= PK else -1
k2 = 0
if st2 >= 0:
    while k2 < len(out2) and st2 + k2 < len(exp2) and out2[k2] == exp2[st2 + k2]: k2 += 1
check(snd.returncode == 0 and st2 >= 0 and k2 >= 60 * PK and max(abs(v) for v in struct.unpack("<%dh" % (min(len(out2), 4 * PK) // 2), out2[:4 * PK])) > 10000,
      f"sender volume -144 dB plays at unity through audiod: {k2 / PK:.0f} packets byte-exact UNscaled from packet {st2 / PK:.2f} (exit {snd.returncode})")
raop_frames = len(out2) // 4 + raop_frames
ev = events()
tk2 = [e for e in ev if e[1] == "takeover" and e[3] == "airplay"]
check(len(tk2) == 2, f"audiod: the -144 dB source arbitrated normally (took over) ({tk2[-1:]})")
# the sender pauses 2.5 s before TEARDOWN, so the quiet release (RELEASE_MS) comes first and the
# TEARDOWN's STOP finds nothing active; either way exactly one release and nothing after it
check(len(rl) == 1 and (rl[0][4] == "stop" or rl[0][4].startswith("quiet")) and tk[0][0] < rl[0][0],
      f"audiod: airplay released once (quiet or STOP), no re-takeover ({rl})")

# ---------------------------------------------------------------- 2. cspot-ios3 --sink-test -> audiod
if CSPOT:
    # B1.7: the default initial volume (50 %) goes through the log curve (-30 dB -> gain 0.0316) and is sent
    # as VOLUME before the tone; audiod applies it: the tone's peak in the output must be 8000 * 0.0316.
    before = os.path.getsize(OUT) // 4
    cs = subprocess.run([CSPOT, "--sink-test", "1", "--audiod-sock", SOCK, "--no-status"], capture_output=True, text=True)
    open(f"{TMP}/cspot-vol.log", "w").write(cs.stdout + cs.stderr)
    time.sleep(1.5)
    quiet_tone = open(OUT, "rb").read()[before * 4:]
    ql = struct.unpack("<%dh" % (len(quiet_tone) // 2), quiet_tone)[0::2] if quiet_tone else [0]
    exp_peak = 8000 * 10 ** (-1.5)
    check(cs.returncode == 0 and abs(max(ql) - exp_peak) <= 0.15 * exp_peak,
          f"cspot default initial volume 50 % through the log curve reaches audiod: tone peak {max(ql)} (expect ~{int(exp_peak)})")
    time.sleep(2.5)  # let audiod release the source before the full-volume run
    t0 = time.time()
    cs = subprocess.run([CSPOT, "--sink-test", "2", "--audiod-sock", SOCK, "--no-status", "--initial-volume", "100"], capture_output=True, text=True)
    open(f"{TMP}/cspot.log", "w").write(cs.stdout + cs.stderr)
    took = time.time() - t0
    check(cs.returncode == 0 and 1.8 <= took <= 3.5, f"cspot-ios3 --sink-test 2 exits 0 and is paced by SocketSink ({took:.1f} s)")
    time.sleep(1.5)
    tone = open(OUT, "rb").read()[(before + len(quiet_tone) // 4) * 4:]
    tf = len(tone) // 4
    left = struct.unpack("<%dh" % (tf * 2), tone)[0::2]
    neg = sum(1 for a, b in zip(left, left[1:]) if a >= 0 and b < 0)  # positive -> negative crossings: 440 per second
    exp_frames = 88200 - 400 * 44.1  # 2 s minus the takeover window (the source starts from silence)
    check(abs(tf - exp_frames) <= 2000, f"audiod played the tone from the takeover on: {tf} frames (expect ~{int(exp_frames)})")
    check(abs(neg - 440 * tf / 44100) <= 0.05 * 440 * tf / 44100 and max(left) > 7000,
          f"it is a 440 Hz tone: {neg} cycles in {tf / 44100:.2f} s (expect ~{int(440 * tf / 44100)}), peak {max(left)}")
    ev = events()
    tk = [e for e in ev if e[1] == "takeover" and e[3] == "spotify"]
    rl = [e for e in ev if e[1] == "release" and e[2] == "spotify"]
    check(len(tk) == 2 and len(rl) == 2 and all(r[4] == "stop" for r in rl), f"audiod: spotify took over twice (both tone runs) and was released by STOP each time ({tk + rl})")
else:
    print("skip cspot-ios3 (no host binary given)")

audiod.terminate(); audiod.wait()
# ---------------------------------------------------------------- 3. B1.9: absolute Spotify volume
# The first audiod ran with the default --spotify-volume absolute but no hardware backend on the host
# (AUDIOD_MOCK_HW_VOLUME unset): it must have fallen back to the software gain, which is what the
# B1.7 peak checks above exercised.
alog = open(f"{TMP}/audiod.log").read()
check("falling back to software gain" in alog, "audiod: absolute requested, hardware unavailable -> logged fallback to software gain (B1.7 peaks above prove it)")
if CSPOT:
    SOCK2, OUT2, EVT2, CHG = f"{TMP}/audiod2.sock", f"{TMP}/out2.pcm", f"{TMP}/events2.log", f"{TMP}/hwchange.txt"
    for f in (SOCK2, OUT2, EVT2, CHG):
        if os.path.exists(f): os.unlink(f)
    env = dict(os.environ, AUDIOD_MOCK_HW_VOLUME="0.30", AUDIOD_MOCK_HW_CHANGE_FILE=CHG)
    audiod2 = subprocess.Popen([AUDIOD, "--sock", SOCK2, "--pcm-out", OUT2, "--event-log", EVT2, "--no-status", "--no-silence",
                                "--spotify-volume", "absolute", "--max-volume", "40", "--wake-on-play", "on"],
                               stdout=open(f"{TMP}/audiod2.log", "w"), stderr=subprocess.STDOUT, env=env)
    for _ in range(50):
        if os.path.exists(SOCK2): break
        time.sleep(0.05)
    # cspot: 2 s tone, the "user" moves the Connect slider to 80 % at 0.8 s (clamped to the 40 % ceiling);
    # meanwhile the "phone buttons" set the hardware to 0.60 at ~0.4 s (the change file)
    def buttons():
        time.sleep(0.4); open(CHG, "w").write("0.60\n")
    threading.Thread(target=buttons, daemon=True).start()
    cs = subprocess.run([CSPOT, "--sink-test", "2", "--sink-test-volume", "80", "--audiod-sock", SOCK2, "--no-status", "--initial-volume", "50"],
                        capture_output=True, text=True, env=env)
    clog = cs.stdout + cs.stderr
    open(f"{TMP}/cspot-abs.log", "w").write(clog)
    time.sleep(1.5)
    audiod2.terminate(); audiod2.wait()
    out2 = open(OUT2, "rb").read()
    left2 = struct.unpack("<%dh" % (len(out2) // 2), out2)[0::2] if out2 else [0]
    check(cs.returncode == 0 and "audiod drives the HARDWARE volume (absolute): level 30 %" in clog,
          "cspot learned from WELCOME that the slider drives the hardware, level 30 % (--initial-volume 50 ignored, nothing set at startup)")
    check(max(left2) > 7900, f"absolute mode: Spotify plays at UNITY gain under the hardware level: tone peak {max(left2)} (software mode gave 252)")
    ev2 = [l.split() for l in open(EVT2)]
    hw = [e for e in ev2 if e[1] == "hwvolume"]
    sets = [e for e in hw if e[2] == "spotify"]
    ext = [e for e in hw if e[2] == "external"]
    check(len(sets) == 1 and sets[0][3] == "0.400" and "slider 52428" in " ".join(sets[0]) and "clamped" in " ".join(sets[0]),
          f"slider 80 % -> hardware set to the --max-volume 40 ceiling, logged as clamped ({[' '.join(e) for e in sets]})")
    check(len(ext) == 1 and ext[0][3] == "0.600", f"the 'phone buttons' change (0.30 -> 0.60) was picked up and reported ({[' '.join(e) for e in ext]})")
    check("audiod reports hardware volume absolute: 60 %" in clog and "audiod reports hardware volume absolute: 40 %" in clog,
          "cspot received both HWVOLUME reports (buttons -> 60 %, clamp -> 40 %) to forward to the controller")
    wk = [e for e in ev2 if e[1] == "wake"]
    check(len(wk) == 1 and wk[0][3] == "spotify" and wk[0][4] == "wake+launch", f"wake-on-play fired once for the none -> spotify start ({[' '.join(e) for e in wk]})")
    check("set 0.300" not in " ".join(" ".join(e) for e in hw) and "set 0.500" not in " ".join(" ".join(e) for e in hw),
          "nothing was set at startup (no set to the initial 30 % or to --initial-volume 50 %)")
print("\nevents:")
for e in events(): print(f"  {e[0]:6d} {e[1]:12s} {e[2]:>8s} -> {e[3]:<8s} {e[4]}")
print("\n" + (f"{len(fails)} FAILURES" if fails else "all producer checks passed"))
sys.exit(1 if fails else 0)
