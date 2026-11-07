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
2. (optional) cspot-ios3 --sink-test 2: two seconds of a 440 Hz tone through its audiod sink;
   audiod's output must grow by ~2 s of tone (frame count and zero crossings), and the event log
   must show spotify taking over and being released by STOP.
"""
import os, struct, subprocess, sys, time

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
# the sender pauses 2.5 s before TEARDOWN, so the quiet release (RELEASE_MS) comes first and the
# TEARDOWN's STOP finds nothing active; either way exactly one release and nothing after it
check(len(rl) == 1 and (rl[0][4] == "stop" or rl[0][4].startswith("quiet")) and tk[0][0] < rl[0][0],
      f"audiod: airplay released once (quiet or STOP), no re-takeover ({rl})")

# ---------------------------------------------------------------- 2. cspot-ios3 --sink-test -> audiod
if CSPOT:
    t0 = time.time()
    cs = subprocess.run([CSPOT, "--sink-test", "2", "--audiod-sock", SOCK, "--no-status"], capture_output=True, text=True)
    open(f"{TMP}/cspot.log", "w").write(cs.stdout + cs.stderr)
    took = time.time() - t0
    check(cs.returncode == 0 and 1.8 <= took <= 3.5, f"cspot-ios3 --sink-test 2 exits 0 and is paced by SocketSink ({took:.1f} s)")
    time.sleep(1.5)
    tone = open(OUT, "rb").read()[raop_frames * 4:]
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
    check(len(tk) == 1 and len(rl) == 1 and rl[0][4] == "stop", f"audiod: spotify took over once and was released by STOP ({tk + rl})")
else:
    print("skip cspot-ios3 (no host binary given)")

audiod.terminate(); audiod.wait()
print("\nevents:")
for e in events(): print(f"  {e[0]:6d} {e[1]:12s} {e[2]:>8s} -> {e[3]:<8s} {e[4]}")
print("\n" + (f"{len(fails)} FAILURES" if fails else "all producer checks passed"))
sys.exit(1 if fails else 0)
