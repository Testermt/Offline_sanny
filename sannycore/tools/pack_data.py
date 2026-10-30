#!/usr/bin/env python3
"""Pack Sanny Builder's `data` folder into one file (sannydata.pack) for the browser engine.

usage: python3 pack_data.py <sanny data dir> [out.pack] [--modes sa,sa_mobile,...]

Format: "SBPK1\\0" | u32 jsonLen (LE) | {"files":[{"p":"sa/x.ini","s":size},...]} | file bytes in order
Only text-like data files are packed (ini/txt/xml/dat/ide/db/json...). optypes.txt is generated from
<data>/sa_sbl/sa.json when gen_optypes.py is next to this script.
"""
import json, os, struct, subprocess, sys

EXT = {'.ini', '.txt', '.xml', '.dat', '.ide', '.db', '.cfg', '.csv'}
MAX = 8 * 1024 * 1024

def main():
    a = [x for x in sys.argv[1:] if not x.startswith('--')]
    if not a:
        print(__doc__); sys.exit(2)
    src = a[0]; out = a[1] if len(a) > 1 else 'sannydata.pack'
    modes = None
    if '--modes' in sys.argv:
        modes = set(sys.argv[sys.argv.index('--modes') + 1].lower().split(','))
    files = []
    for root, dirs, names in os.walk(src):
        rel_root = os.path.relpath(root, src).replace('\\', '/')
        top = rel_root.split('/')[0].lower()
        if top.endswith('_sbl'):          # JSON libraries of the new SBL language: not used by sannycore
            dirs[:] = []; continue
        if modes and rel_root != '.' and top not in modes and top not in ('common', 'shared'):
            # keep every folder that a selected mode may reference (mode.xml extends / shared lists)
            pass
        for n in names:
            if os.path.splitext(n)[1].lower() not in EXT: continue
            p = os.path.join(root, n)
            if os.path.getsize(p) > MAX: continue
            files.append((os.path.relpath(p, src).replace('\\', '/'), p))
    if modes:
        keep = []
        for rel, p in files:
            top = rel.split('/')[0].lower()
            if '/' not in rel or top in modes: keep.append((rel, p))
        files = keep
    tools = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'gen_optypes.py')
    sbl = os.path.join(src, 'sa_sbl', 'sa.json')
    blobs = []
    if os.path.exists(tools) and os.path.exists(sbl) and not any(r == 'optypes.txt' for r, _ in files):
        try:
            data = subprocess.run([sys.executable, tools, sbl], capture_output=True, check=True).stdout
            blobs.append(('optypes.txt', data))
        except Exception as e:
            print('warning: optypes.txt not generated:', e)
    for rel, p in sorted(files):
        with open(p, 'rb') as f: blobs.append((rel, f.read()))
    head = json.dumps({'files': [{'p': r, 's': len(b)} for r, b in blobs]}, separators=(',', ':')).encode()
    with open(out, 'wb') as f:
        f.write(b'SBPK1\0'); f.write(struct.pack('<I', len(head))); f.write(head)
        for _, b in blobs: f.write(b)
    print('packed %d files, %.1f KB -> %s' % (len(blobs), os.path.getsize(out) / 1024, out))

if __name__ == '__main__':
    main()
