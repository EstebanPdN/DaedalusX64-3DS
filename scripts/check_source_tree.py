#!/usr/bin/env python3
"""Reject accidental publication of local binaries, ROMs, saves and private build data."""
import pathlib, subprocess, sys
root=pathlib.Path(__file__).resolve().parents[1]
# zlib.map is an inherited linker version script, not a generated map.
source_exceptions={'Source/third_party/zlib/zlib.map'}
paths=subprocess.check_output(['git','ls-files','-z'],cwd=root).decode().split('\0')
blocked={'.cia','.3dsx','.elf','.map','.dmp','.z64','.n64','.v64','.sav','.sra','.fla','.mpk'}
bad=[p for p in paths if p and p not in source_exceptions and (pathlib.Path(p).suffix.lower() in blocked or p.startswith(('.deps/','Artifacts/','Build/','Research/')))]
if bad:
 print('Forbidden tracked output:',*bad,sep='\n');sys.exit(1)
print('PASS: no tracked installer, executable, ROM, personal save or private build directory')
