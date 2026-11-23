#!/usr/bin/env python3
"""B1.16.16: the dock remote's target. audiod keeps a paused source of ours as the remote's target (and
writes it to <status-dir>/remote-target) until it disconnects, sends STOP, another source of ours becomes
active, or audio that is not ours plays; with nothing active, CONTROL verbs go to that target (TOGGLE and
PLAY as PLAY). The take-or-pass decision is the tweak's own C++ (DockRemoteCore.cpp, built as
dockremote_probe) run against the files this audiod writes. "Other audio" is mocked with
AUDIOD_MOCK_OTHER_AUDIO_FILE. Usage:
  test_remote_target.py PATH_TO_dock-audiod PATH_TO_dockremote_probe
"""
import os, shutil, socket, struct, subprocess, sys, threading, time
AUDIOD, PROBE = sys.argv[1], sys.argv[2]
GAP, TAKEOVER, HOLD, RELEASE, QUIET_RELEASE, IDLE_STOP, LEAD = 1000, 400, 500, 1500, 3000, 5000, 750
HELLO, AUDIO, FLUSH, STOP, VOLUME, WELCOME, HWVOLUME, PREEMPTED, CONTROL = 1, 2, 3, 4, 6, 7, 8, 9, 10
PLAY, PAUSE, TOGGLE, NEXT, PREV = 1, 2, 3, 4, 5
TMP = "/tmp/audiod-remote-target"
fails = []
def check(ok, msg):
    print(("ok   " if ok else "FAIL ") + msg)
    if not ok: fails.append(msg)

def start(tag):
    d = f"{TMP}/{tag}"
    shutil.rmtree(d, ignore_errors=True); os.makedirs(d)
    with open(f"{d}/settings.conf", "w") as f: f.write("audiod.dock_remote=on\n")
    sock = f"{d}/a.sock"
    env = dict(os.environ, AUDIOD_MOCK_OTHER_AUDIO_FILE=f"{d}/other-audio")
    args = [AUDIOD, "--sock", sock, "--pcm-out", f"{d}/out.pcm", "--status-dir", d, "--no-silence",
            "--spotify-volume", "software", "--airplay-volume-mode", "software", "--wake-on-play", "off",
            "--takeover-ms", str(TAKEOVER), "--hold-ms", str(HOLD), "--release-ms", str(RELEASE),
            "--quiet-release-ms", str(QUIET_RELEASE), "--idle-stop-ms", str(IDLE_STOP), "--producing-gap-ms", str(GAP),
            "--lead-ms", str(LEAD)]
    p = subprocess.Popen(args, stdout=open(f"{d}/audiod.log", "w"), stderr=subprocess.STDOUT, env=env)
    for _ in range(100):
        if os.path.exists(sock): break
        time.sleep(0.05)
    time.sleep(0.1)
    return p, d, sock

def read(d, name):
    try: return open(f"{d}/{name}").read()
    except OSError: return None

def log(d): return open(f"{d}/audiod.log").read()

def probe(d, sock, code=0):
    return subprocess.run([PROBE, d, sock, str(code)], capture_output=True, text=True).stdout.strip()

def other_audio(d, on):
    with open(f"{d}/other-audio.tmp", "w") as f: f.write("1" if on else "0")
    os.rename(f"{d}/other-audio.tmp", f"{d}/other-audio")

class V2Client:
    def __init__(self, sock, name, sample):
        self.s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM); self.s.connect(sock)
        self.s.sendall(struct.pack("<II", HELLO, 40) + struct.pack("<BBBBI32s", 2, 0, 2, 16, 44100, name.encode()))
        self.name, self.sample, self.buf, self.controls, self.stop, self.t = name, sample, b"", [], False, None
        self.halt = False
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
    def play(self, ms=60000):
        """Stream loud audio until pause() (a playing source); a pause stops the bytes, as cspot does."""
        self.halt = False
        def run():
            chunk = struct.pack("<hh", self.sample, self.sample) * 882
            n = (ms + LEAD) // 20; t0 = time.monotonic()
            for i in range(n):
                if self.stop or self.halt: break
                dl = (i*20 - LEAD)/1000 - (time.monotonic() - t0)
                if dl > 0: time.sleep(dl)
                self.send(AUDIO, chunk)
        self.t = threading.Thread(target=run, daemon=True); self.t.start()
    def pause(self):
        self.halt = True
        if self.t: self.t.join()
    def close(self):
        self.stop = True; self.halt = True
        if self.t: self.t.join()
        try: self.s.shutdown(socket.SHUT_RDWR)  # a close alone does not end a recv() blocked in the reader thread
        except OSError: pass
        try: self.s.close()
        except OSError: pass

def control(sock, verb):
    """What DockRemote's forward() sends: a one-shot 'control' client with one verb."""
    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM); s.connect(sock)
    s.sendall(struct.pack("<II", HELLO, 40) + struct.pack("<BBBBI32s", 2, 0, 2, 16, 44100, b"control"))
    s.sendall(struct.pack("<II", CONTROL, 1) + bytes([verb]))
    time.sleep(0.2); s.close()

def press(d, sock, code, verb):
    """One remote press as the tweak handles it: ask the condition; if taken, send the verb to audiod."""
    r = probe(d, sock, code)
    if r == "take": control(sock, verb)
    return r

def wait_for(fn, secs):
    t0 = time.monotonic()
    while time.monotonic() - t0 < secs:
        if fn(): return True
        time.sleep(0.05)
    return fn()

def stop(p):
    p.terminate(); p.wait()

# --- 1. the trace of 2026-11-22: play/pause pauses, the source is released, play/pause resumes ---------
p, d, sock = start("resume")
check(read(d, "remote-target") == "none\n", f"at start-up remote-target is 'none' ({read(d, 'remote-target')!r})")
check("remote target (B1.16.16): " + d + "/remote-target" in log(d), "start-up line names the remote-target file")
sp = V2Client(sock, "spotify", 8000); sp.play()
check(wait_for(lambda: read(d, "remote-target") == "spotify\n", 3), f"playing: remote-target 'spotify' ({read(d, 'remote-target')!r})")
r1 = press(d, sock, 0, TOGGLE); time.sleep(0.2)
check(r1 == "take" and sp.controls == [TOGGLE], f"play/pause while playing: taken, TOGGLE reaches spotify ({r1}, {sp.controls})")
sp.pause()  # cspot paused: no more bytes
check(wait_for(lambda: read(d, "active-source") == "none\n", 3), "paused: audiod releases spotify (active-source 'none')")
time.sleep(0.3)
check(read(d, "remote-target") == "spotify\n" and "release: 'spotify' -> 'none' (quiet" in log(d),
      f"after the quiet release the paused spotify is still the remote target ({read(d, 'remote-target')!r})")
r2 = press(d, sock, 0, TOGGLE); time.sleep(0.2)
check(r2 == "take" and sp.controls == [TOGGLE, PLAY],
      f"play/pause while paused: TAKEN and audiod resumes spotify with PLAY ({r2}, {sp.controls})")
check("control: play -> 'spotify' (resume) (paused remote target)" in log(d), "audiod logged the resume to the paused remote target")
r3 = press(d, sock, 3, NEXT); time.sleep(0.2)
r4 = press(d, sock, 4, PREV); time.sleep(0.2)
check(r3 == "take" and r4 == "take" and sp.controls[2:] == [NEXT, PREV],
      f"next and previous while paused: taken and delivered to spotify ({r3}, {r4}, {sp.controls})")
check(probe(d, sock, 22) == "pass" and probe(d, sock, 25) == "pass", "menu and down still pass while a paused target exists")
# 10 s later: still the target (no time limit), and the file was rewritten (mtime) without a change
m0 = os.stat(f"{d}/remote-target").st_mtime
time.sleep(10.6)
check(read(d, "remote-target") == "spotify\n" and probe(d, sock, 0) == "take", "after 11 s paused: still the target (no time limit)")
check(os.stat(f"{d}/remote-target").st_mtime > m0, "the file is rewritten every 10 s while unchanged (mtime moved)")
# a resume: spotify plays again, then disconnects while paused
sp.play(); time.sleep(1.2)
check(read(d, "remote-target") == "spotify\n" and read(d, "active-source") == "spotify\n", "playing again: active and the target")
sp.pause(); time.sleep(2.0)
sp.close()
check(wait_for(lambda: read(d, "remote-target") == "none\n", 2), f"paused spotify disconnects: remote-target 'none' ({read(d, 'remote-target')!r})")
check("remote target: 'spotify' -> 'none' ('spotify' disconnected)" in log(d), "audiod logged the disconnect clearing the target")
check(probe(d, sock, 0) == "pass", "after the disconnect play/pause passes to the system")
stop(p)
check(read(d, "remote-target") == "none\n", "audiod stopped: remote-target 'none'")
check(not os.path.exists(f"{d}/remote-target.tmp") and not os.path.exists(f"{d}/active-source.tmp"), "no temporary file left behind")

# --- 2. other audio while paused takes the remote back, for good until one of ours plays -----------
p, d, sock = start("other")
sp = V2Client(sock, "spotify", 8000); sp.play(); time.sleep(1.2)
other_audio(d, True)
time.sleep(1.0)
check(read(d, "remote-target") == "spotify\n", "other audio while spotify PLAYS (active): spotify stays the target (as today)")
other_audio(d, False)
sp.pause(); wait_for(lambda: read(d, "active-source") == "none\n", 3); time.sleep(0.6)
check(read(d, "remote-target") == "spotify\n", "paused, no other audio: spotify is the target")
other_audio(d, True)
check(wait_for(lambda: read(d, "remote-target") == "none\n", 1.5), f"other audio plays while paused: remote-target 'none' within the poll ({read(d, 'remote-target')!r})")
check("(other audio is playing (not ours)" in log(d), "audiod logged the other audio clearing the target")
check(probe(d, sock, 0) == "pass", "then play/pause passes to the system (the Music app)")
other_audio(d, False); time.sleep(1.2)
check(read(d, "remote-target") == "none\n" and probe(d, sock, 0) == "pass", "the other audio stops: the keys are NOT taken back")
n = len(sp.controls)
control(sock, NEXT); time.sleep(0.2)
check(len(sp.controls) == n, "with no target, NEXT with nothing active is still ignored (B1.16 rule)")
sp.play(); time.sleep(1.2)
check(read(d, "remote-target") == "spotify\n" and probe(d, sock, 0) == "take", "spotify plays again: the target again, taken")
sp.close(); stop(p)

# --- 3. two sources: another of ours takes over, then pauses ----------------------------------------
p, d, sock = start("two")
sp = V2Client(sock, "spotify", 8000); ap = V2Client(sock, "airplay", 9000)
sp.play(); time.sleep(1.2); sp.pause(); wait_for(lambda: read(d, "active-source") == "none\n", 3); time.sleep(0.3)
check(read(d, "remote-target") == "spotify\n", "spotify paused: the target")
ap.play(); time.sleep(1.2)
check(read(d, "remote-target") == "airplay\n" and "remote target: 'spotify' -> 'airplay' ('airplay' took over)" in log(d),
      f"airplay starts: the target is airplay ({read(d, 'remote-target')!r})")
ap.pause(); wait_for(lambda: read(d, "active-source") == "none\n", 3); time.sleep(0.3)
check(read(d, "remote-target") == "airplay\n", "airplay paused: airplay stays the target, not the earlier spotify")
press(d, sock, 0, TOGGLE); time.sleep(0.2)
check(ap.controls == [PLAY] and sp.controls == [], f"play/pause resumes airplay only (airplay={ap.controls}, spotify={sp.controls})")
ap.close(); time.sleep(0.3)
check(read(d, "remote-target") == "none\n", "airplay disconnects: no target (spotify, still connected and paused, does not come back)")
sp.close(); stop(p)

# --- 4. STOP from the paused target (an AirPlay sender left: raop's teardown) -----------------------
p, d, sock = start("stop")
ap = V2Client(sock, "airplay", 9000); ap.play(); time.sleep(1.2); ap.pause()
wait_for(lambda: read(d, "active-source") == "none\n", 3); time.sleep(0.3)
check(read(d, "remote-target") == "airplay\n", "airplay paused: the target")
ap.send(STOP); time.sleep(0.3)
check(read(d, "remote-target") == "none\n" and "'airplay' sent STOP" in log(d), f"STOP while paused clears the target ({read(d, 'remote-target')!r})")
ap.play(); time.sleep(1.2); ap.send(STOP); ap.pause(); time.sleep(0.3)
check(read(d, "remote-target") == "none\n", "STOP while active (session torn down while playing) clears it as well")
ap.close(); stop(p)

# --- 5. the file is replaced atomically: a reader never sees a partial or empty file ----------------
p, d, sock = start("atomic")
seen, stop_reader = set(), [False]
def reader():
    while not stop_reader[0]:
        try: seen.add(open(f"{d}/remote-target").read())
        except OSError: seen.add("<missing>")
t = threading.Thread(target=reader, daemon=True); t.start()
sp = V2Client(sock, "spotify", 8000)
for i in range(3):
    sp.play(); time.sleep(1.0); sp.pause(); time.sleep(0.2)
    other_audio(d, True); time.sleep(0.8); other_audio(d, False)
stop_reader[0] = True; t.join()
check(seen <= {"none\n", "spotify\n"} and len(seen) == 2, f"a reader during 6 changes saw only whole values ({sorted(seen)})")
sp.close(); stop(p)

# --- 6. audiod killed: a leftover target does not make the tweak take keys --------------------------
p, d, sock = start("crash")
sp = V2Client(sock, "spotify", 8000); sp.play(); time.sleep(1.2); sp.pause()
wait_for(lambda: read(d, "active-source") == "none\n", 3)
check(probe(d, sock, 0) == "take", "paused target with audiod running: taken")
p.kill(); p.wait()
check(read(d, "remote-target") == "spotify\n" and probe(d, sock, 0) == "pass",
      "audiod killed: the file still says 'spotify', the socket is dead, the key passes")
sp.close()

print("\n" + (f"{len(fails)} FAILURES" if fails else "all remote target checks passed"))
sys.exit(1 if fails else 0)
