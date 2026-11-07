import sys, struct
out = open(sys.argv[1], 'rb').read(); exp = open(sys.argv[2], 'rb').read(); N = int(sys.argv[3]); PK = 352 * 4
# common prefix (pre-FLUSH segment)
i = 0
while i < min(len(out), len(exp)) and out[i] == exp[i]: i += 1
pre = i // PK
# post-FLUSH segment: remainder of out must be a prefix of exp[N*PK:]
rest = out[pre * PK:]; post = exp[N * PK:]
j = 0
while j < min(len(rest), len(post)) and rest[j] == post[j]: j += 1
ok = (pre >= N - 140) and (j == len(rest)) and (len(rest) // PK >= 20)
print(f"received {len(out)//PK} packets; pre-flush exact prefix {pre}/{N} packets; post-flush {len(rest)//PK} packets, {j//PK} matching -> {'PASS' if ok else 'FAIL'}")
sys.exit(0 if ok else 1)
