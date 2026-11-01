import math, sys, collections, re
d = open(sys.argv[1], 'rb').read()
def ent(b):
    c = collections.Counter(b); n = len(b)
    return -sum(v / n * math.log2(v / n) for v in c.values())
for name, ro, rs in [('.text', 0x400, 0x1317600), ('.rdata', 0x1319a00, 0x1db600), ('.data', 0x14f5000, 0x20c00), ('.bind', 0x1571c00, 0x62400)]:
    seg = d[ro:ro + rs]
    print(name, 'entropy=%.3f' % ent(seg[:4000000]))
# typical x86 MSVC prologue count in .text
t = d[0x400:0x400 + 0x1317600]
print('push ebp;mov ebp,esp count:', t.count(b'\x55\x8b\xec'))
print('ascii strings in .text sample:', len(re.findall(rb'[ -~]{12,}', t[:2000000])))
print('SteamStub markers:', [m for m in (b'.bind', b'SteamDRM', b'drmp.dll', b'SteamStub') if m in d])
