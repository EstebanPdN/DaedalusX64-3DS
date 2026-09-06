#!/usr/bin/env python3
"""Build pinned CTR dependencies privately; never install into DEVKITPRO."""
import argparse, concurrent.futures, hashlib, json, os, pathlib, shutil, subprocess, tarfile, urllib.request
ROOT = pathlib.Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser(); p.add_argument('--download-only', action='store_true'); p.add_argument('--jobs', type=int, default=4)
args = p.parse_args()
deps = ROOT / '.deps'; prefix = deps / 'prefix'; logs = deps / 'logs'
for d in (deps/'archives', deps/'sources', prefix/'include', prefix/'lib', logs): d.mkdir(parents=True, exist_ok=True)
lock = json.loads((ROOT/'scripts/dependencies.json').read_text())
def fetch(item):
 name, spec = item; archive = deps/'archives'/f'{name}-{spec["revision"]}.tar.gz'; dest = deps/'sources'/name
 if not archive.exists():
  request = urllib.request.Request(f'https://codeload.github.com/{spec["repository"]}/tar.gz/{spec["revision"]}', headers={'User-Agent':'DaedalusX64-EPD-build'})
  with urllib.request.urlopen(request, timeout=120) as response, archive.with_suffix('.tmp').open('wb') as output: shutil.copyfileobj(response, output)
  archive.with_suffix('.tmp').rename(archive)
 digest = hashlib.sha256(archive.read_bytes()).hexdigest()
 if spec.get('sha256') and digest != spec['sha256']: raise RuntimeError(f'{name}: archive checksum mismatch')
 if not dest.exists():
  temp = deps/'sources'/f'{name}-extract'; temp.mkdir(exist_ok=True)
  with tarfile.open(archive) as tar:
   for member in tar.getmembers():
    target = (temp/member.name).resolve()
    if not target.is_relative_to(temp.resolve()): raise RuntimeError('Unsafe archive path')
   tar.extractall(temp, filter='data')
  entries = list(temp.iterdir()); assert len(entries)==1
  entries[0].rename(dest); temp.rmdir()
 return name, digest
with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
 for name, digest in pool.map(fetch, lock.items()): print(name, digest, flush=True)
if args.download_only: raise SystemExit(0)
dkp = pathlib.Path(os.environ.get('DEVKITPRO','/opt/devkitpro')); arm = dkp/'devkitARM'; ctr = dkp/'libctru'
env = dict(os.environ, DEVKITPRO=str(dkp), DEVKITARM=str(arm), CTRULIB=str(ctr))
env['PATH'] = str(dkp/'tools/bin')+os.pathsep+str(arm/'bin')+os.pathsep+env['PATH']
arch = '-march=armv6k -mtune=mpcore -mfloat-abi=hard -mfpu=vfp -mtp=soft -D__3DS__'
def run(name, command, cwd=None):
 print('Building:', name, flush=True)
 with (logs/(name+'.log')).open('w') as log:
  result=subprocess.run(command,cwd=cwd,env=env,stdout=log,stderr=subprocess.STDOUT)
 if result.returncode: raise RuntimeError(f'{name} failed; see {logs/(name+".log")}')
common = ['-DCMAKE_SYSTEM_NAME=Generic','-DCMAKE_SYSTEM_PROCESSOR=armv6k','-DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY',f'-DCMAKE_C_COMPILER={arm}/bin/arm-none-eabi-gcc',f'-DCMAKE_AR={arm}/bin/arm-none-eabi-gcc-ar',f'-DCMAKE_RANLIB={arm}/bin/arm-none-eabi-gcc-ranlib',f'-DCMAKE_C_FLAGS={arch} -O2 -ffunction-sections -fdata-sections',f'-DCMAKE_INSTALL_PREFIX={prefix}','-DCMAKE_BUILD_TYPE=Release']
for name, options in [('zlib',['-DZLIB_BUILD_TESTING=OFF','-DZLIB_BUILD_SHARED=OFF']),('libpng',['-DPNG_SHARED=OFF','-DPNG_TESTS=OFF','-DPNG_TOOLS=OFF',f'-DZLIB_LIBRARY={prefix}/lib/libz.a',f'-DZLIB_INCLUDE_DIR={prefix}/include','-DPNG_ARM_NEON=off'])]:
 build=deps/'build'/name
 run(name+'-configure',['cmake','-S',str(deps/'sources'/name),'-B',str(build),*common,*options])
 run(name+'-build',['cmake','--build',str(build),'--parallel',str(args.jobs)])
 run(name+'-install',['cmake','--install',str(build)])
pica=deps/'sources/picagl'
cflags=f'{arch} -std=gnu11 -O2 -g -Wall -Werror -mword-relocations -ffunction-sections -fdata-sections -I{ctr}/include -I{pica}/include -I{pica}/build'
run('picagl-build',['make',f'-j{args.jobs}',f'CFLAGS={cflags}'],pica)
shutil.copytree(pica/'include',prefix/'include',dirs_exist_ok=True);shutil.copy2(pica/'lib/libpicaGL.a',prefix/'lib')
imgui=deps/'sources/imgui'
cflags=f'{arch} -g -Wall -O2 -mword-relocations -fomit-frame-pointer -ffunction-sections -I{ctr}/include -I{prefix}/include'
run('imgui-build',['make',f'-j{args.jobs}',f'CFLAGS={cflags}'],imgui)
shutil.copy2(imgui/'libimgui.a',prefix/'lib')
for h in imgui.glob('*.h'): shutil.copy2(h,prefix/'include')
print('Dependencies ready:',prefix)
