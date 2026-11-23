#!/usr/bin/env python3
"""B1.12 part B: when AirPlay loses the output, dock-raop sends the sender a DACP pause, verifies
from the stream, and disconnects a sender that ignores it (with a cool-down). A mock DACP server in
raop_sender decides honor/ignore/none. Usage: test_airplay_preempt.py AUDIOD RAOP
"""
import os, socket, struct, subprocess, sys, threading, time
AUDIOD, RAOP = sys.argv[1], sys.argv[2]
HERE = os.path.dirname(os.path.abspath(__file__))
SENDER = os.path.join(HERE, "..", "..", "airplay", "hosttest", "raop_sender.py")
TAKEOVER, HOLD, RELEASE, IDLE_STOP, LEAD = 400, 500, 1500, 5000, 750
VERIFY_MS, COOLDOWN_MS = 1500, 3000
HELLO, AUDIO = 1, 2
TMP = "/tmp/audiod-airpreempt"; os.makedirs(TMP, exist_ok=True)
fails = []
def check(ok, msg):
    print(("ok   " if ok else "FAIL ") + msg)
    if not ok: fails.append(msg)

def spotify_takeover(sock, secs=3.0):
    """a v2 client that streams loud audio to take the output from airplay"""
    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM); s.connect(sock)
    s.sendall(struct.pack("<II", HELLO, 40) + struct.pack("<BBBBI32s", 2, 0, 2, 16, 44100, b"spotify"))
    stop = {"v": False}
    def run():
        chunk = struct.pack("<hh", 0x4444, 0x4444) * 882
        n = int((secs*1000 + LEAD)//20); t0 = time.monotonic()
        for i in range(n):
            if stop["v"]: break
            d = (i*20 - LEAD)/1000 - (time.monotonic() - t0)
            if d > 0: time.sleep(d)
            try: s.sendall(struct.pack("<II", AUDIO, len(chunk)) + chunk)
            except OSError: break
    th = threading.Thread(target=run, daemon=True); th.start()
    return s, stop, th

def run_case(mode, port, dacp_port, verify=VERIFY_MS):
    base = f"{TMP}/{mode}"
    sock, out, evt = f"{base}.sock", f"{base}.pcm", f"{base}.ev"
    for f in (sock, out, evt):
        if os.path.exists(f): os.unlink(f)
    ad = subprocess.Popen([AUDIOD, "--sock", sock, "--pcm-out", out, "--event-log", evt, "--no-status", "--no-silence",
                           "--takeover-ms", str(TAKEOVER), "--hold-ms", str(HOLD), "--release-ms", str(RELEASE),
                           "--idle-stop-ms", str(IDLE_STOP), "--lead-ms", str(LEAD), "--wake-on-play", "off"],
                          stdout=open(f"{base}-audiod.log","w"), stderr=subprocess.STDOUT)
    for _ in range(50):
        if os.path.exists(sock): break
        time.sleep(0.05)
    env = dict(os.environ, AUDIOD_RAOP_DACP_ADDR=f"127.0.0.1:{dacp_port}")
    rp = subprocess.Popen([RAOP, "--audiod-sock", sock, "--port", str(port), "--no-status", "--name", "hosttest",
                           "--latency-ms", "500", "--preempt-verify-ms", str(verify), "--preempt-cooldown-ms", str(COOLDOWN_MS)],
                          stdout=open(f"{base}-raop.log","w"), stderr=subprocess.STDOUT, env=env)
    time.sleep(0.6)
    snd = subprocess.Popen([sys.executable, SENDER, "--port", str(port), "--codec", "pcm", "--preempt-test", mode,
                            "--dacp-listen", str(dacp_port), "--stream-secs", "8"],
                           stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    time.sleep(1.6)  # airplay becomes active
    sp, stop, th = spotify_takeover(sock, 4.0)  # spotify takes over -> airplay pre-empted
    out_txt, _ = snd.communicate(timeout=20)
    res = {}
    for tok in (out_txt.split("PREEMPT",1)[1].split("\n")[0].split() if "PREEMPT" in out_txt else []):
        k,_,v = tok.partition("="); res[k]=int(v) if v.isdigit() else v
    stop["v"]=True; th.join(); sp.close()
    raoplog = open(f"{base}-raop.log").read()
    rp.terminate(); rp.wait(); ad.terminate(); ad.wait()
    return res, raoplog, sock, port

# honour: pause received, stream stops, NOT disconnected
res, log, _, _ = run_case("honor", 5300, 6300)
check(res.get("pause_requests",0) >= 1, f"honor: the mock DACP server received a pause ({res})")
check(res.get("paused")==1 and res.get("disconnected")==0, f"honor: sender paused and was NOT disconnected ({res})")
check("verified paused" in log, "honor: raop verified the pause from the stream and kept the session")

# ignore: pause received, but stream continues -> disconnected after the verify window
res, log, _, _ = run_case("ignore", 5301, 6301)
check(res.get("pause_requests",0) >= 1, f"ignore: the mock DACP server received a pause ({res})")
check(res.get("disconnected")==1, f"ignore: a sender that ignored the pause was disconnected ({res})")
check("still streaming after" in log and "disconnecting" in log, "ignore: raop logged still-streaming -> disconnect")

# none: no DACP offered -> disconnected after the window
res, log, sock, port = run_case("none", 5302, 6302)
check(res.get("pause_requests",0) == 0, f"none: no DACP pause attempted ({res})")
check(res.get("disconnected")==1 and "no remote control" in log, f"none: a sender with no remote control was disconnected ({res})")

# cool-down: force a disconnect, then a reconnect from the same client inside the cool-down is refused
# (453, no takeover); a reconnect after the cool-down is a normal new stream.
base = f"{TMP}/cool"; sock, out, evt = f"{base}.sock", f"{base}.pcm", f"{base}.ev"
for f in (sock, out, evt):
    if os.path.exists(f): os.unlink(f)
ad = subprocess.Popen([AUDIOD, "--sock", sock, "--pcm-out", out, "--event-log", evt, "--no-status", "--no-silence",
                       "--takeover-ms", str(TAKEOVER), "--hold-ms", str(HOLD), "--release-ms", str(RELEASE),
                       "--idle-stop-ms", str(IDLE_STOP), "--lead-ms", str(LEAD), "--wake-on-play", "off"],
                      stdout=open(f"{base}-audiod.log","w"), stderr=subprocess.STDOUT)
for _ in range(50):
    if os.path.exists(sock): break
    time.sleep(0.05)
env = dict(os.environ, AUDIOD_RAOP_DACP_ADDR="127.0.0.1:6304")
rp = subprocess.Popen([RAOP, "--audiod-sock", sock, "--port", "5304", "--no-status", "--name", "hosttest",
                       "--latency-ms", "500", "--preempt-verify-ms", str(VERIFY_MS), "--preempt-cooldown-ms", str(COOLDOWN_MS)],
                      stdout=open(f"{base}-raop.log","w"), stderr=subprocess.STDOUT, env=env)
time.sleep(0.6)
snd = subprocess.Popen([sys.executable, SENDER, "--port", "5304", "--codec", "pcm", "--preempt-test", "none", "--stream-secs", "6"],
                       stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT)
time.sleep(1.6)
sp, stop, th = spotify_takeover(sock, 2.0)
time.sleep(VERIFY_MS/1000 + 1.2)  # let the verify window pass and the disconnect happen
stop["v"]=True; th.join(); sp.close(); snd.wait(timeout=10)
# inside the cool-down: a new ANNOUNCE must be refused with 453
r1 = subprocess.run([sys.executable, SENDER, "--port", "5304", "--codec", "pcm", "--packets", "10", "--preempt-test", "none"],
                    capture_output=True, text=True)
tk_before = sum(1 for l in open(evt) if "takeover" in l and "airplay" in l)
check(r1.returncode != 0 and "FAIL ANNOUNCE 200" in r1.stdout, "cool-down: a reconnect inside the window is refused (ANNOUNCE not 200)")
# after the cool-down: a new session works again
time.sleep(COOLDOWN_MS/1000 + 0.3)
r2 = subprocess.run([sys.executable, SENDER, "--port", "5304", "--codec", "pcm", "--packets", "60", "--preempt-test", "none"],
                    capture_output=True, text=True)
check(r2.returncode == 0 and "ok   ANNOUNCE 200" in r2.stdout, "cool-down: a reconnect after the window is a normal new stream")
tk_after = sum(1 for l in open(evt) if "takeover" in l and "airplay" in l)
check(tk_after > tk_before, "cool-down: the post-cool-down session takes the output as usual")
rp.terminate(); rp.wait(); ad.terminate(); ad.wait()

print("\n" + (f"{len(fails)} FAILURES" if fails else "all airplay-preempt checks passed"))
sys.exit(1 if fails else 0)
