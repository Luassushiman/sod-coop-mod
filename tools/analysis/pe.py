import struct, sys, datetime
p = sys.argv[1]
d = open(p, 'rb').read()
pe = struct.unpack_from('<I', d, 0x3c)[0]
mach, nsec, ts = struct.unpack_from('<HHI', d, pe + 4)
optsz, chars = struct.unpack_from('<HH', d, pe + 20)
opt = pe + 24
magic = struct.unpack_from('<H', d, opt)[0]
is64 = magic == 0x20b
print('machine=%#x 64bit=%s timestamp=%s' % (mach, is64, datetime.datetime.utcfromtimestamp(ts)))
if is64:
    ep, = struct.unpack_from('<I', d, opt + 16); base, = struct.unpack_from('<Q', d, opt + 24)
    sub, dllch = struct.unpack_from('<HH', d, opt + 68); ddir = opt + 112
else:
    ep, = struct.unpack_from('<I', d, opt + 16); base, = struct.unpack_from('<I', d, opt + 28)
    sub, dllch = struct.unpack_from('<HH', d, opt + 68); ddir = opt + 96
print('entry=%#x imagebase=%#x subsystem=%d dllchars=%#x (ASLR=%s)' % (ep, base, sub, dllch, bool(dllch & 0x40)))
secs = []
so = opt + optsz
for i in range(nsec):
    n, vs, va, rs, ro = struct.unpack_from('<8sIIII', d, so + i * 40)
    secs.append((n.rstrip(b'\0').decode(), va, vs, ro, rs))
    print('  sec %-8s va=%#x vsize=%#x raw=%#x' % secs[-1][:4])
def r2o(rva):
    for n, va, vs, ro, rs in secs:
        if va <= rva < va + max(vs, rs): return rva - va + ro
def cstr(o):
    e = d.index(b'\0', o); return d[o:e].decode('latin1')
# exports
erva, esz = struct.unpack_from('<II', d, ddir)
if erva:
    eo = r2o(erva)
    nn, an, aon = struct.unpack_from('<III', d, eo + 24)[0], *struct.unpack_from('<II', d, eo + 32)
    names = [cstr(r2o(struct.unpack_from('<I', d, r2o(an) + 4 * i)[0])) for i in range(nn)]
    print('EXPORTS (%d):' % nn, names[:60])
irva, isz = struct.unpack_from('<II', d, ddir + 8)
io = r2o(irva)
while True:
    oft, _, _, nm, ft = struct.unpack_from('<IIIII', d, io)
    if nm == 0: break
    dll = cstr(r2o(nm)); funcs = []
    t = r2o(oft or ft)
    while True:
        v = struct.unpack_from('<Q' if is64 else '<I', d, t)[0]
        if v == 0: break
        if v >> (63 if is64 else 31): funcs.append('#%d' % (v & 0xffff))
        else: funcs.append(cstr(r2o(v & 0x7fffffff) + 2))
        t += 8 if is64 else 4
    print('IMPORT %s (%d): %s' % (dll, len(funcs), ', '.join(funcs) if len(sys.argv) > 2 else ', '.join(funcs[:25])))
    io += 20
