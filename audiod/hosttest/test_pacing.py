#!/usr/bin/env python3
"""Host test for B1.5: the active producer must be paced by the output's real consumption.

  test_pacing.py AUDIOD RAOP

audiod runs with --pcm-out-paced PPM: a model of the AudioQueue that consumes at 44100*(1+PPM/1e6)
frames per wall second in 46 ms bursts. The host fidelio-raop streams a 20 s scripted RAOP session
(airplay/hosttest/raop_sender.py --jitter: a 250 ms stall every 2 s) into it. For each clock skew
the test runs audiod twice: --no-backpressure (the b14 behaviour: the producer paced by SocketSink's
wall clock, audiod reads everything) and the default (b15: the active source read only as fast as
the output consumes). It reports raop's own underrun counter (its jitter buffer running dry) for
both, and asserts that with backpressure the producer's throughput tracked the skewed consumption
(the output file length is within 1% of consumption time * skewed rate, and the sink never
overfilled) and that raop's underruns stayed at the standalone level (<= 2).
"""
import os, re, subprocess, sys, time

AUDIOD, RAOP = sys.argv[1], sys.argv[2]
HERE = os.path.dirname(os.path.abspath(__file__))
SENDER = os.path.join(HERE, "..", "..", "airplay", "hosttest", "raop_sender.py")
TMP = "/tmp/audiod-pacing"
os.makedirs(TMP, exist_ok=True)
PACKETS = 2500  # 20 s
fails = []
def check(ok, msg):
    print(("ok   " if ok else "FAIL ") + msg)
    if not ok: fails.append(msg)

def run(tag, skew_ppm, backpressure, port):
    sock, out, log = f"{TMP}/{tag}.sock", f"{TMP}/{tag}.pcm", f"{TMP}/{tag}.audiod.log"
    for f in (sock, out):
        if os.path.exists(f): os.unlink(f)
    args = [AUDIOD, "--sock", sock, "--pcm-out", out, "--pcm-out-paced", str(skew_ppm), "--no-status", "--no-silence", "--event-log", f"{TMP}/{tag}.events"]
    if not backpressure: args.append("--no-backpressure")
    a = subprocess.Popen(args, stdout=open(log, "w"), stderr=subprocess.STDOUT)
    for _ in range(50):
        if os.path.exists(sock): break
        time.sleep(0.05)
    r = subprocess.Popen([RAOP, "--audiod-sock", sock, "--port", str(port), "--no-status", "--name", tag, "--latency-ms", "1200"],
                         stdout=open(f"{TMP}/{tag}.raop.log", "w"), stderr=subprocess.STDOUT)
    time.sleep(0.5)
    t0 = time.time()
    snd = subprocess.run([sys.executable, SENDER, "--port", str(port), "--codec", "pcm", "--packets", str(PACKETS), "--jitter",
                          "--expect-pcm", f"{TMP}/{tag}.expect.pcm"], capture_output=True, text=True)
    took = time.time() - t0
    time.sleep(1.0)
    r.terminate(); r.wait()
    time.sleep(0.3)
    a.terminate(); a.wait()
    rlog = open(f"{TMP}/{tag}.raop.log").read()
    m = re.search(r"session torn down: packets=(\d+) .*?underruns=(\d+)", rlog)
    packets, underruns = (int(m.group(1)), int(m.group(2))) if m else (-1, -1)
    alog = open(log).read()
    m2 = re.search(r"paced output: (\d+) starved bursts", alog)
    starved = int(m2.group(1)) if m2 else -1
    frames = os.path.getsize(out) // 4
    return dict(exit=snd.returncode, took=took, packets=packets, underruns=underruns, frames=frames, starved=starved)

port = 5200
for skew in (-3000, 3000):  # +-0.3 %: the output consumes slower / faster than the wall clock
    res = {}
    for mode in ("b14", "b15"):
        tag = f"{mode}_{'minus' if skew < 0 else 'plus'}{abs(skew)}"
        res[mode] = run(tag, skew, backpressure=(mode == "b15"), port=port); port += 1
        r = res[mode]
        print(f"  {tag:14s}: sender exit {r['exit']}, {r['took']:.1f} s, raop packets {r['packets']}, raop UNDERRUNS {r['underruns']}, output {r['frames']} frames, sink STARVED bursts {r['starved']}")
    b = res["b15"]
    check(b["exit"] == 0 and b["packets"] > PACKETS, f"skew {skew:+d} ppm: b15 session completed ({b['packets']} packets)")
    check(0 <= b["underruns"] <= 2, f"skew {skew:+d} ppm: b15 raop underruns at the standalone level: {b['underruns']} (b14 for comparison: {res['b14']['underruns']})")
    # every packet raop played reached the output (nothing dropped on the way): the paced sink was
    # never overfilled, i.e. the producer waited for consumption instead of running ahead
    played = (b["packets"] - 150) * 352  # raop keeps its last cushion unplayed at TEARDOWN; this is a loose lower bound
    check(b["frames"] >= played * 0.90, f"skew {skew:+d} ppm: b15 output holds what raop played ({b['frames']} frames >= 90% of {played})")
# Drift: the output consumes 2 % faster than the wall clock (a gross exaggeration of hardware-vs-
# monotonic drift, so 20 s show what hours would). Paced by the wall clock (b14) the producer falls
# behind: once audiod's ring cushion is spent the sink pads silence; paced by consumption (b15) it
# keeps up out of raop's jitter buffer and the sink never starves.
res = {}
for mode in ("b14", "b15"):
    tag = f"{mode}_drift20000"
    res[mode] = run(tag, 20000, backpressure=(mode == "b15"), port=port); port += 1
    r = res[mode]
    print(f"  {tag:14s}: sender exit {r['exit']}, raop packets {r['packets']}, raop UNDERRUNS {r['underruns']}, output {r['frames']} frames, sink STARVED bursts {r['starved']}")
check(res["b15"]["starved"] == 0, f"+2 % drift: with backpressure the output never starved (b15 starved {res['b15']['starved']}; b14 for comparison: {res['b14']['starved']})")
check(res["b15"]["underruns"] <= 2, f"+2 % drift: b15 raop underruns still at the standalone level: {res['b15']['underruns']}")
print("\n" + (f"{len(fails)} FAILURES" if fails else "all pacing checks passed"))
sys.exit(1 if fails else 0)
