# DaedalusX64 for Nintendo 3DS

An independent development fork maintained by **EstebanPdN**, based on [MasterFeizz's DaedalusX64-3DS](https://github.com/masterfeizz/DaedalusX64-3DS) and the [DaedalusX64 project](https://github.com/DaedalusX64/daedalus). This is a continuation of their emulator, not a new emulation core written from scratch.

**Status: dev1, source development and local hardware evaluation. No public binary release.** New 3DS/New 2DS XL is the primary target; Old 3DS/2DS remains an experimental, performance-limited target. Successful compilation and host tests do not establish game compatibility or real-console stability.

## Initial changes

- Correct millisecond waits and paired tick conversions without claiming a fix for runaway game speed.
- Give thread arguments a valid lifetime; join the New 3DS audio worker before freeing its resources and wait for task completion before reporting the guest audio event.
- Replace the CTR audio queue with a bounded single-producer/single-consumer queue, fix the first-sample offset, prevent interpolation overreads, and fill NDSP underruns with silence.
- Write native saves and mempaks through a checked temporary-file transaction, retaining a previous `.bak` and keeping failed writes dirty for retry during the session.
- Check executable-memory allocation/permissions after services are ready, correct the permission-restoration byte count, and bound FlashRAM block accesses.
- Unwind completed initialization steps, handle empty ROM lists and failed launches, and shut down UI/graphics/audio in order.
- Build with private, pinned dependencies and package current RomFS sources for both CIA and 3DSX.

The renderer, RSP microcodes, MIPS instruction translation and game-specific compatibility settings are inherited. Several substantial upstream limitations remain; see [development notes](docs/DEVELOPMENT.md). No upstream issue is declared fixed solely from a code change.

## Build and test

See [BUILDING.md](docs/BUILDING.md) for dependencies and local packaging. Run the production-code host tests with:

```sh
scripts/test_host.sh
```

The repository contains source, inherited resource assets, build scripts and documentation. Build outputs, ROMs and personal saves are excluded. CI runs host tests and a source-content check; it does not upload CIA/3DSX artifacts or publish releases.

## Local installation layout

A local CIA uses title ID `000400000DAED400`, different from the original port's `000400000DAED300`, and the HOME Menu name **DaedalusX64 EPD**. The inherited banner artwork is retained with credit.

Copy the **contents** of `Data/` into `sdmc:/3ds/DaedalusX64-EPD/`. Put your own ROM files in `sdmc:/3ds/DaedalusX64-EPD/Roms/`. The app creates `SaveGames/` and `SaveStates/` there. DSP firmware is expected at `sdmc:/3ds/dspfirm.cdc`; firmware and ROM files are not included.

Existing settings and saves are not migrated automatically. If testing old saves, first keep a separate copy of the original `DaedalusX64` folder, then copy the desired files into the corresponding EPD folder. Native save formats remain unchanged. Save states have no new compatibility guarantee.

Use the [hardware test protocol](docs/HARDWARE-TESTING.md) before calling a build stable. This fork is intended for homebrew-capable consoles with the permissions required by the inherited dynamic recompiler.

## Credits and license

MasterFeizz created the Nintendo 3DS port. Daedalus and DaedalusX64 contributors created the emulator on which it depends. See [CREDITS.md](CREDITS.md), the preserved [upstream README](docs/UPSTREAM-README.md), and individual source notices. The project's GPL terms are in [copying.txt](copying.txt); third-party components retain their respective licenses. This project is not affiliated with Nintendo.
