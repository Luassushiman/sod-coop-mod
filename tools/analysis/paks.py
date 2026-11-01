import zipfile, sys, os, collections
root = sys.argv[1]
for dp, _, fs in os.walk(root):
    for f in fs:
        if not f.endswith('.pak'): continue
        p = os.path.join(dp, f)
        try:
            z = zipfile.ZipFile(p)
        except Exception as e:
            print(f, 'ERR', e); continue
        il = z.infolist()
        ext = collections.Counter(os.path.splitext(i.filename)[1].lower() for i in il)
        enc = sum(1 for i in il if i.flag_bits & 1)
        meth = collections.Counter(i.compress_type for i in il)
        dirs = collections.Counter('/'.join(i.filename.split('/')[:2]) for i in il)
        print('== %s files=%d encrypted=%d methods=%s' % (os.path.relpath(p, root), len(il), enc, dict(meth)))
        print('   ext:', ext.most_common(14))
        print('   dirs:', dirs.most_common(12))
