# Building locally

## Requirements

- devkitPro devkitARM and libctru, with 3DS tools (`3dsxtool`, `smdhtool`, `mkromfs3ds`, `picasso`, `bin2s`). The initial build used GCC 15.2.0.
- CMake, make, Python 3.12 or newer, and a native [makerom 0.19.0](https://github.com/3DSGuy/Project_CTR/releases/tag/makerom-v0.19.0).
- Clang with AddressSanitizer/UndefinedBehaviorSanitizer for host tests. Tests require C++17; the CTR application uses C++14.
- Internet access on the first dependency bootstrap.

The dependency bootstrap downloads exact revisions of zlib, libpng, picaGL and imgui-picagl and verifies archive SHA-256 values from `scripts/dependencies.json`. It builds into `.deps/prefix` and does not install into or replace global devkitPro libraries. libctru/devkitARM themselves come from your installed toolchain; record their versions when comparing builds. This is dependency pinning, not a claim of bit-identical builds across toolchains or operating systems.

```sh
export DEVKITPRO=/opt/devkitpro
python3 scripts/build_3ds.py --makerom /path/to/makerom
scripts/test_host.sh
```

Outputs are local to `build-3ds/`: `DaedalusX64-EPD.cia`, `DaedalusX64-EPD.3dsx`, `DaedalusX64.elf`, a linker map and generated resources. `--build-dir` selects another directory; `--skip-dependencies` reuses an already built private prefix. Do not use this flag after changing dependency pins without rebuilding them.

CIA packaging uses makerom's `RomFs.RootPath` builder to generate the IVFC tree from source files. The 3DSX uses `mkromfs3ds`'s raw filesystem image; these are deliberately different container forms of the same resource files. Neither format relies on the inherited prebuilt `romfs.bin` or `daedalus.smdh`.

The HOME Menu title is `DaedalusX64 EPD`; title ID is `000400000DAED400`. The original memory-mode and New 3DS service/core permissions are retained. Title-ID differentiation is relative to the original port, not a global registry reservation.

For a local installation bundle, copy the contents of `Data/` to `3ds/DaedalusX64-EPD/` on the SD card. Keep `Roms/`, `SaveGames/` and `SaveStates/` under that directory. Do not redistribute ROMs or DSP firmware. The local ELF and map should be retained for crash symbolication.

## Validation commands

```sh
scripts/test_host.sh
python3 scripts/check_source_tree.py
ctrtool --verify --contents=inspection/content build-3ds/DaedalusX64-EPD.cia
```

`ctrtool` must be installed separately if desired. Homebrew containers have unofficial signatures; distinguish expected signature warnings from failed content hashes. Hardware tests remain required even when all content hashes pass.

CI intentionally runs source checks and host tests only, with no installer artifact upload and no release creation. Other inherited platform targets have not been revalidated by this CTR-focused work.

The initial GCC 15.2 toolchain merges an ARMv7 ELF attribute from the discarded libgcc `_sync_dmb.o` member. Both the reference build and this fork exhibit it. Inspection of the retained executable found the ARMv6 CP15 barrier sequence and no ARMv7 barrier/MOVW/MOVT/bitfield instructions. An ELF attribute alone is therefore not a complete instruction-set audit. Application and private dependency compilation explicitly targets ARMv6K/VFP, with libpng NEON disabled.
