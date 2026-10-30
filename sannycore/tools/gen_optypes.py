#!/usr/bin/env python3
"""Builds optypes.txt (compact param source/type table) from Sanny's data/sa_sbl/sa.json.
usage: gen_optypes.py <sanny data dir>/sa_sbl/sa.json > optypes.txt
Line format:  XXXX=g:i,l:f,x:i   (source: x literal, g var_global, l var_local, a var_any, o other ; type: i int, f float, o other)
Only commands without output params are listed (their param order == binary order)."""
import json, sys
j = json.load(open(sys.argv[1], encoding='utf-8'))
S = {'literal': 'x', 'var_global': 'g', 'var_local': 'l', 'var_any': 'a'}
T = {'int': 'i', 'float': 'f'}
seen = {}
for ext in j['extensions']:
    for c in ext.get('commands', []):
        if c.get('output') or not c.get('input'): continue
        seen.setdefault(c['id'].upper(), ','.join(S.get(p.get('source'), 'o') + ':' + T.get(p.get('type'), 'o') for p in c['input']))
for k in sorted(seen): print(f'{k}={seen[k]}')
