#!/usr/bin/env python3
"""B1.16: the transport control channel. A controller connects to audiod, sends a CONTROL verb, and
audiod routes it to the ACTIVE source (or, for PLAY with nothing active, the most-recently-active
connected source). The controller is not an arbiter source. Usage:
  test_control.py PATH_TO_fidelio-audiod PATH_TO_control_send
"""
import os, socket, struct, subprocess, sys, threading, time
AUDIOD = sys.argv[1]
CONTROL_SEND = sys.argv[2] if len(sys.argv) > 2 else None
GAP, TAKEOVER, HOLD, RELEASE, QUIET_RELEASE, IDLE_STOP, LEAD = 1000, 400, 500, 1500, 3000, 5000, 750
HELLO, AUDIO, FLUSH, STOP, VOLUME, WELCOME, HWVOLUME, PREEMPTED, CONTROL = 1, 2, 3, 4, 6, 7, 8, 9, 10
PLAY, PAUSE, TOGGLE, NEXT, PREV = 1, 2, 3, 4, 5
TMP = "/tmp/audiod-control"; os.makedirs(TMP, exist_ok=True)
fails = []
def check(ok, msg):
    print(("ok   " if ok else "FAIL ") + msg)
    if not ok: fails.append(msg)

def start():
    sock, out = f"{TMP}/a.sock", f"{TMP}/out.pcm"
    for f in (sock, out):
        if os.path.exists(f): os.unlink(f)
    args = [AUDIOD, "--sock", sock, "--pcm-out", out, "--no-status", "--no-silence",
            "--spotify-volume", "software", "--wake-on-play", "off", "--takeover-ms", str(TAKEOVER),
            "--hold-ms", str(HOLD), "--release-ms", str(RELEASE), "--quiet-release-ms", str(QUIET_RELEASE),
            "--idle-stop-ms", str(IDLE_STOP), "--producing-gap-ms", str(GAP), "--lead-ms", str(LEAD)]
    p = subprocess.Popen(args, stdout=open(f"{TMP}/audiod.log", "w"), stderr=subprocess.STDOUT)
    for _ in range(50):
        if os.path.exists(sock): break
        time.sleep(0.05)
    return p, sock

class V2Client:
    def __init__(self, sock, name, sample):
        self.s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM); self.s.connect(sock)
        self.s.sendall(struct.pack("<II", HELLO, 40) + struct.pack("<BBBBI32s", 2, 0, 2, 16, 44100, name.encode()))
        self.name, self.sample, self.buf, self.controls, self.stop, self.t = name, sample, b"", [], False, None
        threading.Thread(target=self._rx, daemon=True).start()
    def _rx(self):
        while not self.stop:
            try: d = self.s.recv(4096)
            except OSError: break
            if not d: break
            self.buf += d
            while len(self.buf) >= 8:
                t, n = struct.unpack("<II", self.buf[:8])
                if len(self.buf) < 8 + n: break
                pl = self.buf[8:8+n]; self.buf = self.buf[8+n:]
                if t == CONTROL and len(pl) >= 1: self.controls.append(pl[0])
    def send(self, t, p=b""):
        try: self.s.sendall(struct.pack("<II", t, len(p)) + p)
        except OSError: pass
    def play(self, ms):
        def run():
            chunk = struct.pack("<hh", self.sample, self.sample) * 882
            n = (ms + LEAD) // 20; t0 = time.monotonic()
            for i in range(n):
                if self.stop: break
                d = (i*20 - LEAD)/1000 - (time.monotonic() - t0)
                if d > 0: time.sleep(d)
                self.send(AUDIO, chunk)
        self.t = threading.Thread(target=run, daemon=True); self.t.start()
    def wait(self):
        if self.t: self.t.join()
    def close(self):
        self.stop = True
        try: self.s.close()
        except OSError: pass

def send_control_inline(sock, verb):
    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM); s.connect(sock)
    s.sendall(struct.pack("<II", HELLO, 40) + struct.pack("<BBBBI32s", 2, 0, 2, 16, 44100, b"control"))
    s.sendall(struct.pack("<II", CONTROL, 1) + bytes([verb]))
    time.sleep(0.2); s.close()

# --- 1. route each verb to the active source -------------------------------------------------------
p, sock = start()
sp = V2Client(sock, "spotify", 8000)
sp.play(4000)
time.sleep(1.0)  # become active
for v in (PLAY, PAUSE, TOGGLE, NEXT, PREV):
    send_control_inline(sock, v); time.sleep(0.15)
time.sleep(0.3)
check(sp.controls == [PLAY, PAUSE, TOGGLE, NEXT, PREV], f"all five verbs routed to the active source in order ({sp.controls})")

# --- 2. a controller is NOT an arbiter source (does not steal the output or appear as a source) -----
# (the spotify source stays active across the control bursts: no new 'active-source' churn)
log = open(f"{TMP}/audiod.log").read()
check("is a transport controller" in log, "audiod logged the control client as a controller, not a source")
check(log.count("control: play") >= 1 and "-> 'spotify'" in log, "audiod logged routing the verb to spotify")
sp.close(); p.terminate(); p.wait()

# --- 3. two sources: only the ACTIVE one gets the verb ---------------------------------------------
p, sock = start()
a = V2Client(sock, "spotify", 8000); a.play(6000); time.sleep(0.8)
b = V2Client(sock, "airplay", 9000); b.play(4000); time.sleep(1.2)  # airplay takes over
send_control_inline(sock, NEXT); time.sleep(0.3)
check(b.controls == [NEXT] and a.controls == [], f"only the active source (airplay) got NEXT (airplay={b.controls}, spotify={a.controls})")
a.close(); b.close(); p.terminate(); p.wait()

# --- 4. PLAY with nothing active resumes the most-recently-active source ---------------------------
p, sock = start()
sp = V2Client(sock, "spotify", 8000); sp.play(1200); time.sleep(0.8)  # active, then it stops producing
sp.wait()  # audio ended
time.sleep(2.2)  # released; nothing active now
send_control_inline(sock, PLAY); time.sleep(0.3)
check(sp.controls and sp.controls[-1] == PLAY, f"PLAY with nothing active resumed the last source (spotify got {sp.controls})")
log = open(f"{TMP}/audiod.log").read()
check("(resume)" in log, "audiod logged the resume routing")
# PAUSE/NEXT with nothing active is a no-op (not routed anywhere)
before = len(sp.controls)
send_control_inline(sock, NEXT); time.sleep(0.3)
check(len(sp.controls) == before, "NEXT with nothing active is ignored (not routed)")
sp.close(); p.terminate(); p.wait()

# --- 5. the shared control helper (control_send: the app / DockRemote path) ------------------------
if CONTROL_SEND:
    p, sock = start()
    sp = V2Client(sock, "spotify", 8000); sp.play(4000); time.sleep(1.0)
    r = subprocess.run([CONTROL_SEND, sock, str(NEXT)], capture_output=True, text=True)
    time.sleep(0.3)
    check("sent" in r.stdout and sp.controls == [NEXT], f"audiod_send_control helper delivered NEXT ({r.stdout.strip()}, got {sp.controls})")
    sp.close(); p.terminate(); p.wait()

print("\n" + (f"{len(fails)} FAILURES" if fails else "all control checks passed"))
sys.exit(1 if fails else 0)
