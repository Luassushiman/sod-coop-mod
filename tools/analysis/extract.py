import zipfile, struct, zlib, sys, os
pak, out = sys.argv[1], sys.argv[2]
z=zipfile.ZipFile(pak); f=open(pak,'rb'); n=0
for i in z.infolist():
    if i.is_dir(): continue
    f.seek(i.header_offset); h=f.read(30); a,b=struct.unpack_from('<HH',h,26); f.seek(i.header_offset+30+a+b); raw=f.read(i.compress_size)
    data = zlib.decompress(raw,15) if i.compress_type==8 else raw
    p=os.path.normpath(os.path.join(out, i.filename.replace(chr(92),'/')))
    assert p.startswith(os.path.normpath(out))
    os.makedirs(os.path.dirname(p),exist_ok=True); open(p,'wb').write(data); n+=1
print('extracted',n)
