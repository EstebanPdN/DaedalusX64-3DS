#!/usr/bin/env python3
"""Build locally with a private dependency prefix; never publish artifacts."""
import argparse, os, pathlib, shutil, subprocess, sys
ROOT = pathlib.Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser()
p.add_argument('--build-dir', type=pathlib.Path, default=ROOT/'build-3ds')
p.add_argument('--makerom', help='Path to a native makerom executable')
p.add_argument('--jobs', type=int, default=4)
p.add_argument('--skip-dependencies', action='store_true')
args = p.parse_args()
dkp = pathlib.Path(os.environ.get('DEVKITPRO','/opt/devkitpro'))
env = dict(os.environ, DEVKITPRO=str(dkp), DEVKITARM=str(dkp/'devkitARM'))
env['PATH'] = str(dkp/'tools/bin')+os.pathsep+str(dkp/'devkitARM/bin')+os.pathsep+env['PATH']
makerom = args.makerom or shutil.which('makerom', path=env['PATH'])
if not makerom: p.error('makerom is required: supply --makerom or add it to PATH')
if not args.skip_dependencies:
    subprocess.run([sys.executable,str(ROOT/'scripts/bootstrap_3ds.py'),'--jobs',str(args.jobs)],env=env,check=True)
subprocess.run(['cmake','-S',str(ROOT/'Source'),'-B',str(args.build_dir.resolve()),'-DCTR_RELEASE=1',
    '-DCMAKE_TOOLCHAIN_FILE='+str(ROOT/'Tools/3dstoolchain.cmake'),'-DCTR_MAKEROM='+str(pathlib.Path(makerom).resolve())],env=env,check=True)
subprocess.run(['cmake','--build',str(args.build_dir.resolve()),'--parallel',str(args.jobs)],env=env,check=True)
print('Local outputs:',args.build_dir.resolve())
