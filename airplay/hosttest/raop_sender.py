#!/usr/bin/env python3
"""Scripted RAOP sender for testing fidelio-raop on the host (no mDNS, no real audio).

  raop_sender.py --port 5000 --codec pcm|alac [--encrypt] [--alac-tool PATH] --status-dir DIR --expect-pcm FILE

Does: OPTIONS (Apple-Challenge, verifies Apple-Response with the AirPort public key), ANNOUNCE
(L16 or ALAC fmtp; optional RSA-OAEP wrapped AES key + iv), SETUP, RECORD, RTP audio (with a
deliberate gap so a resend request is exercised, answered from a backlog), SET_PARAMETER
(volume, DMAP metadata, JPEG artwork, progress), GET_PARAMETER, FLUSH, more audio, TEARDOWN.
Writes the PCM it sent to --expect-pcm so the receiver's --pcm-out can be compared.
"""
import argparse, base64, os, random, socket, struct, subprocess, sys, time, math

HERE = os.path.dirname(os.path.abspath(__file__))
KEY_PATH = os.path.join(HERE, "..", "airport_express_key.pem")
PUB_PATH = os.path.join(HERE, "airport_express_pub.pem")
subprocess.run(["openssl", "rsa", "-in", KEY_PATH, "-pubout", "-out", PUB_PATH], check=True, capture_output=True)
RSA_N = int(subprocess.run(["openssl", "rsa", "-in", KEY_PATH, "-noout", "-modulus"], check=True, capture_output=True, text=True).stdout.split("=")[1].strip(), 16)
RSA_E = 65537
FRAMES = 352

def rsa_oaep_sha1_encrypt(data):
    return subprocess.run(["openssl", "pkeyutl", "-encrypt", "-pubin", "-inkey", PUB_PATH, "-pkeyopt", "rsa_padding_mode:oaep",
                           "-pkeyopt", "rsa_oaep_md:sha1", "-pkeyopt", "rsa_mgf1_md:sha1"], input=data, check=True, capture_output=True).stdout

def aes_cbc_encrypt(key, iv, data):
    return subprocess.run(["openssl", "enc", "-aes-128-cbc", "-nopad", "-K", key.hex(), "-iv", iv.hex()], input=data, check=True, capture_output=True).stdout

ap = argparse.ArgumentParser()
ap.add_argument("--host", default="127.0.0.1")
ap.add_argument("--port", type=int, default=5000)
ap.add_argument("--codec", default="pcm")
ap.add_argument("--encrypt", action="store_true")
ap.add_argument("--alac-tool")
ap.add_argument("--expect-pcm")
ap.add_argument("--packets", type=int, default=400)
ap.add_argument("--volume", type=float, default=-15.0, help="SET_PARAMETER volume in dB (-144 = the AirPlay mute sentinel)")
args = ap.parse_args()

sock = socket.create_connection((args.host, args.port), timeout=5)
local_ip = sock.getsockname()[0]
cseq = 0
fails = []

def check(cond, msg):
    print(("ok   " if cond else "FAIL ") + msg)
    if not cond: fails.append(msg)

def request(method, uri, headers=None, body=b"", ctype=None):
    global cseq
    cseq += 1
    h = {"CSeq": str(cseq), "User-Agent": "raop_sender/1.0", "Client-Instance": "ABCDEF0123456789",
         "DACP-ID": "ABCDEF0123456789", "Active-Remote": "1234567890"}
    if headers: h.update(headers)
    if body:
        h["Content-Type"] = ctype or "text/parameters"
        h["Content-Length"] = str(len(body))
    req = f"{method} {uri} RTSP/1.0\r\n" + "".join(f"{k}: {v}\r\n" for k, v in h.items()) + "\r\n"
    sock.sendall(req.encode("latin-1") + body)
    data = b""
    while b"\r\n\r\n" not in data:
        chunk = sock.recv(4096)
        if not chunk: raise EOFError("receiver closed")
        data += chunk
    head, rest = data.split(b"\r\n\r\n", 1)
    lines = head.decode("latin-1").split("\r\n")
    code = int(lines[0].split()[1])
    rh = {}
    for l in lines[1:]:
        k, _, v = l.partition(":"); rh[k.strip().lower()] = v.strip()
    n = int(rh.get("content-length", "0"))
    while len(rest) < n: rest += sock.recv(4096)
    print(f"  {method} -> {code} {dict((k, v) for k, v in rh.items() if k not in ('cseq', 'server'))}")
    return code, rh, rest[:n]

# --- OPTIONS + Apple-Challenge
chall = os.urandom(16)
code, rh, _ = request("OPTIONS", "*", {"Apple-Challenge": base64.b64encode(chall).decode().rstrip("=")})
check(code == 200, "OPTIONS 200")
check("apple-response" in rh, "Apple-Response present")
if "apple-response" in rh:
    sig = rh["apple-response"]; sig += "=" * (-len(sig) % 4)
    sig = base64.b64decode(sig)
    n = RSA_N; e = RSA_E
    m = pow(int.from_bytes(sig, "big"), e, n).to_bytes((n.bit_length() + 7) // 8, "big")
    # PKCS#1 v1.5 type 1: 00 01 FF..FF 00 || message
    check(m[:2] == b"\x00\x01" and b"\x00" in m[2:], "Apple-Response is PKCS#1 v1.5 type-1 padded")
    msg = m[m.index(b"\x00", 2) + 1:]
    expect = chall + socket.inet_aton(local_ip)  # + hwaddr(6), padded to 32
    check(msg[:20] == expect and len(msg) == 32, f"Apple-Response signs challenge|ip|hwaddr (hwaddr={msg[20:26].hex()})")
check(rh.get("public", "").find("SETUP") >= 0, "Public lists SETUP")

# --- ANNOUNCE
aes_key = aes_iv = None
sdp = f"v=0\r\no=iTunes 1 0 IN IP4 {local_ip}\r\ns=iTunes\r\nc=IN IP4 {args.host}\r\nt=0 0\r\nm=audio 0 RTP/AVP 96\r\n"
if args.codec == "alac":
    sdp += f"a=rtpmap:96 AppleLossless\r\na=fmtp:96 {FRAMES} 0 16 40 10 14 2 255 0 0 44100\r\n"
else:
    sdp += "a=rtpmap:96 L16/44100/2\r\n"
if args.encrypt:
    aes_key, aes_iv = os.urandom(16), os.urandom(16)
    enc = rsa_oaep_sha1_encrypt(aes_key)
    sdp += "a=rsaaeskey:" + base64.b64encode(enc).decode().rstrip("=") + "\r\n"
    sdp += "a=aesiv:" + base64.b64encode(aes_iv).decode().rstrip("=") + "\r\n"
uri = f"rtsp://{local_ip}/1234567890"
code, _, _ = request("ANNOUNCE", uri, body=sdp.encode(), ctype="application/sdp")
check(code == 200, "ANNOUNCE 200")

# --- SETUP
audio_s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM); audio_s.bind(("0.0.0.0", 0))
# Non-blocking: the audio loop polls these once per packet; a 50 ms timeout here made the sender
# ~10x slower than real time, which only an unpaced file sink could follow.
ctrl_s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM); ctrl_s.bind(("0.0.0.0", 0)); ctrl_s.setblocking(False)
tim_s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM); tim_s.bind(("0.0.0.0", 0)); tim_s.setblocking(False)
code, rh, _ = request("SETUP", uri, {"Transport": f"RTP/AVP/UDP;unicast;interleaved=0-1;mode=record;control_port={ctrl_s.getsockname()[1]};timing_port={tim_s.getsockname()[1]}"})
check(code == 200, "SETUP 200")
tr = dict(p.split("=", 1) for p in rh.get("transport", "").split(";") if "=" in p)
server_port, control_port = int(tr.get("server_port", 0)), int(tr.get("control_port", 0))
check(server_port > 0 and control_port > 0, f"SETUP Transport has server_port={server_port} control_port={control_port}")
check(rh.get("session") == "1", "Session header")

# --- RECORD
seq0 = random.randint(0, 60000)
rtp_base = random.randint(0, 2**31)
code, rh, _ = request("RECORD", uri, {"Session": "1", "Range": "npt=0-", "RTP-Info": f"seq={seq0};rtptime={rtp_base}"})
check(code == 200 and rh.get("audio-latency") == "11025", "RECORD 200 with Audio-Latency 11025")

# --- SET_PARAMETER volume / metadata / artwork / progress, GET_PARAMETER
code, _, _ = request("SET_PARAMETER", uri, {"Session": "1"}, f"volume: {args.volume:.6f}\r\n".encode())
check(code == 200, "SET_PARAMETER volume")
def dmap(tag, data): return tag.encode() + struct.pack(">I", len(data)) + data
meta = dmap("mlit", dmap("minm", "Tëst Title".encode()) + dmap("asar", "Some Artist".encode()) + dmap("asal", "An Album".encode()))
code, _, _ = request("SET_PARAMETER", uri, {"Session": "1", "RTP-Info": f"rtptime={rtp_base}"}, meta, "application/x-dmap-tagged")
check(code == 200, "SET_PARAMETER dmap")
jpeg = b"\xff\xd8\xff\xe0" + os.urandom(3000) + b"\xff\xd9"
code, _, _ = request("SET_PARAMETER", uri, {"Session": "1", "RTP-Info": f"rtptime={rtp_base}"}, jpeg, "image/jpeg")
check(code == 200, "SET_PARAMETER artwork")
start = rtp_base - 30 * 44100; end = start + 200 * 44100
code, _, _ = request("SET_PARAMETER", uri, {"Session": "1"}, f"progress: {start}/{rtp_base}/{end}\r\n".encode())
check(code == 200, "SET_PARAMETER progress")
code, _, body = request("GET_PARAMETER", uri, {"Session": "1"}, b"volume\r\n")
check(code == 200 and f"volume: {args.volume:.1f}".encode()[:11] in body, f"GET_PARAMETER volume -> {body!r}")

# --- audio
expect = open(args.expect_pcm, "wb") if args.expect_pcm else None
alac_proc = None
if args.codec == "alac":
    alac_proc = subprocess.Popen([args.alac_tool], stdin=subprocess.PIPE, stdout=subprocess.PIPE)

def pcm_frames(i):
    # 440 Hz left / 660 Hz right plus a per-packet DC tag: the tones alone repeat every 2205 frames
    # (50 ms), which made packet-order analysis ambiguous; the tag makes every packet unique.
    tag = ((i * 2654435761) & 0xFFFF) % 1000 - 500
    out = bytearray()
    for k in range(FRAMES):
        t = (i * FRAMES + k) / 44100.0
        l = int(12000 * math.sin(2 * math.pi * 440 * t)) + tag; r = int(8000 * math.sin(2 * math.pi * 660 * t)) - tag
        out += struct.pack("<hh", l, r)
    return bytes(out)

def payload_for(pcm_le):
    if args.codec == "alac":
        alac_proc.stdin.write(pcm_le); alac_proc.stdin.flush()
        n = struct.unpack("<I", alac_proc.stdout.read(4))[0]
        return alac_proc.stdout.read(n)
    # L16: big-endian
    return b"".join(struct.pack(">hh", *struct.unpack("<hh", pcm_le[j:j + 4])) for j in range(0, len(pcm_le), 4))

def encrypt(p):
    if not aes_key: return p
    whole = len(p) // 16 * 16
    if whole == 0: return p
    return aes_cbc_encrypt(aes_key, aes_iv, p[:whole]) + p[whole:]

backlog = {}
def send_packet(i, first=False, skip=False):
    seq = (seq0 + i) & 0xFFFF
    pcm = pcm_frames(i)
    if expect: expect.write(pcm)
    pl = encrypt(payload_for(pcm))
    pkt = struct.pack(">BBHII", 0x80, 0xE0 if first else 0x60, seq, (rtp_base + i * FRAMES) & 0xFFFFFFFF, 0x12345678) + pl
    backlog[seq] = pkt
    if not skip:
        audio_s.sendto(pkt, (args.host, server_port))

def service_control():
    try:
        while True:
            data, addr = ctrl_s.recvfrom(2048)
            if len(data) >= 8 and (data[1] & 0x7F) == 0x55:
                first, count = struct.unpack(">HH", data[4:8])
                for k in range(count):
                    s = (first + k) & 0xFFFF
                    if s in backlog:
                        p = backlog[s]
                        resp = struct.pack(">BBH", 0x80, 0xD6, s) + p[:1] + b"\x60" + p[2:]
                        ctrl_s.sendto(resp, addr)
                        resent.append(s)
    except (socket.timeout, BlockingIOError):
        pass

def service_timing():
    try:
        while True:
            data, addr = tim_s.recvfrom(64)
            if len(data) >= 32 and (data[1] & 0x7F) == 0x52:
                now = time.time() + 2208988800
                ntp = struct.pack(">II", int(now), int((now % 1) * 2**32))
                tim_s.sendto(struct.pack(">BBH", 0x80, 0xD3, 7) + b"\0" * 4 + data[24:32] + ntp + ntp, addr)
                timing_answered.append(1)
    except (socket.timeout, BlockingIOError):
        pass

resent, timing_answered = [], []
sync_sent = 0
t0 = time.time()
N = args.packets
for i in range(N):
    # drop packets 50..52 on first send so the receiver asks for them (answered from the backlog)
    send_packet(i, first=(i == 0), skip=(50 <= i <= 52))
    if i % 125 == 0:
        now = time.time() + 2208988800
        rtp_now = (rtp_base + i * FRAMES) & 0xFFFFFFFF
        sync = struct.pack(">BBHI", 0x90 if i == 0 else 0x80, 0xD4, 7, (rtp_now - 11025) & 0xFFFFFFFF) + struct.pack(">II", int(now), int((now % 1) * 2**32)) + struct.pack(">I", rtp_now)
        ctrl_s.sendto(sync, (args.host, control_port)); sync_sent += 1
    service_control(); service_timing()
    # real-time pacing: one packet per 7.98 ms
    target = t0 + (i + 1) * FRAMES / 44100.0
    d = target - time.time()
    if d > 0: time.sleep(d)
for _ in range(10): service_control(); service_timing(); time.sleep(0.05)
check(len(resent) >= 3, f"resend requests answered: {sorted(set(resent))}")
print(f"  sync packets sent: {sync_sent}, timing requests answered: {len(timing_answered)}")

# --- FLUSH then a little more audio, then TEARDOWN
code, _, _ = request("FLUSH", uri, {"Session": "1", "RTP-Info": f"seq={(seq0 + N) & 0xFFFF};rtptime={rtp_base + N * FRAMES}"})
check(code == 200, "FLUSH 200")
time.sleep(0.3)
for i in range(N, N + 150):
    send_packet(i)
    service_control(); service_timing()
    target = t0 + (i + 1 + 40) * FRAMES / 44100.0
    d = target - time.time()
    if d > 0: time.sleep(d)
time.sleep(2.5)  # let the receiver drain its jitter buffer (1 s) before TEARDOWN
code, _, _ = request("TEARDOWN", uri, {"Session": "1"})
check(code == 200, "TEARDOWN 200")
if expect: expect.close()
if alac_proc: alac_proc.stdin.close(); alac_proc.wait()
sock.close()
print(f"\n{len(fails)} failures" if fails else "\nall protocol checks passed")
sys.exit(1 if fails else 0)
