import zipfile, sys, collections, re
import zlib, struct
z = zipfile.ZipFile(sys.argv[1]); raw = open(sys.argv[1],'rb')
def rd(i):
    raw.seek(i.header_offset); h = raw.read(30)
    n, e = struct.unpack_from('<HH', h, 26); raw.seek(i.header_offset + 30 + n + e)
    b = raw.read(i.compress_size)
    return zlib.decompress(b, 15) if i.compress_type == 8 else b
il = z.infolist()
heads = collections.Counter()
hits = collections.defaultdict(list)
pats = [rb'Net\.Expose', rb'RMI', rb'IsMultiplayer', rb'gameRules', rb'g_gameRules', rb'Client\.On', rb'Server\.On', rb'System\.IsMultiplayer', rb'CryAction', rb'Vehicle', rb'Inventory']
for i in il:
    if not i.filename.endswith('.lua'): continue
    b = rd(i)
    heads[b[:4]] += 1
    for p in pats:
        if re.search(p, b): hits[p.decode()].append(i.filename)
print('lua headers:', heads.most_common(5))
for k, v in hits.items(): print('%-18s %4d  e.g. %s' % (k, len(v), v[:4]))
subdirs = collections.Counter('/'.join(i.filename.split('/')[:3]) for i in il if i.filename.startswith(('scripts/', 'libs/class3')))
print(subdirs.most_common(40))
print([i.filename for i in il if i.filename.startswith('libs/class3')][:131])
