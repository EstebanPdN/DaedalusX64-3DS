# Development scope and evidence

## Baseline and intent

The stable reference tree is MasterFeizz/DaedalusX64-3DS at `31c5e560d4cbd11d6e5b50e5e42c25b9598a68a9`. The initial branch focuses on resource lifetime, persistence, input validation and reproducible local builds. It does not replace the CPU core, RSP HLE or PICA renderer.

A planning snapshot on 2026-09-06 examined all 116 open issue threads and 173 comments then available in the original 3DS repository, plus critical code paths and related projects. User reports are reproduction leads, not proof of a specific root cause. The source inventory and local research are separate from this public development tree. The reviewed issue list is available at [the original repository](https://github.com/masterfeizz/DaedalusX64-3DS/issues).

## Changes and their limits

| Area | Initial change | Evidence / limitation |
| --- | --- | --- |
| Time | Convert milliseconds to nanoseconds using 64-bit arithmetic; correct the named milliseconds contract and tick caller together | Host tests. The old tick converter returned microseconds and its multiplier cancelled that error; no 1,000x limiter-speed fix is claimed. |
| Thread lifetime | Worker owns a copied heap launch context; audio worker is joinable and joined before teardown | Source/build review; libctru scheduling and shutdown still require hardware tests. |
| Guest audio completion | Wait for the worker before consuming the already scheduled audio-completion event | Preserves the original one-cycle event policy. May reduce overlap and affect performance. This is not a complete job-snapshot or cycle-accuracy redesign. |
| Audio samples | Acquire/release SPSC queue, partial publication, fixed NDSP duration, silence on underrun, bounds on interpolation | Actual CTR resampler compiled into host tests; concurrent queue transfer and malformed-rate tests. Producer waiting is bounded; a stopped consumer can cause samples to be dropped. No allocation or blocking in the refill callback. |
| Persistence | Temporary file, checked write/flush/close, previous-file backup, rollback, dirty flag retained on failure | Fault injection covers write/open/flush/close and rename failures. No guarantee of power-loss atomicity or SD durability; no fsync guarantee. A final failed save at exit can still lose the newest in-memory progress. |
| JIT setup | Allocate lazily after SVC initialization; fail on allocation/permission error; restore permissions in bytes | Cross-build and inspection. Does not yet bound every emitted block or remove privileged service dependence. |
| FlashRAM | Reject an out-of-range 128-byte erase/write offset | Source review. This is not a comprehensive audit of every guest memory access. |
| Lifecycle | Track completed initialization entries, unwind on error, handle empty lists and failed loads, finish GPU work before freeing platform resources | Cross-build and source review; model-specific HOME/sleep/exit behavior awaits console testing. Failed initializer functions must still clean up their own partial work. |
| Packaging | Own title ID and SD directory, generated SMDH and RomFS, private pinned dependency prefix | Local CIA/3DSX generation and container inspection. Existing icon/banner retained. |

## Known remaining work

- Reproduce graphics/framebuffer, microcode, blending, clipping and texture-cache issues before changing renderer behavior. The initial patch does not claim to fix game-specific black screens or missing effects.
- Add byte limits and failure propagation through ARM code emission. Existing fragment-count policies are not sufficient proof of buffer safety.
- Improve malformed/truncated ROM and ROM-cache I/O failure propagation across all consumers.
- Make initialization allocation failure reporting consistent throughout the older core, and report failed saves visibly during the active session.
- Separate owned audio jobs and guest scheduling more thoroughly; profile synchronous and asynchronous modes on both hardware classes.
- Add frame-time, VI rate, audio starvation and memory high-water diagnostics tied to ROM hash, configuration and source revision.
- Review dependency updates individually. Private pins make a build explainable, but do not establish that these library versions are identical to the 2022 release environment.

## Direction after hardware validation

1. Establish repeatable startup, open/close, save/reload, sleep and HOME-menu behavior on New 3DS and Old 3DS using the same ROM/configuration baseline.
2. Capture reproducible traces for a small game set, including CPU-light titles and demanding framebuffer/microcode cases. Compare real elapsed time, guest VI and audio, not the FPS number alone.
3. Fix one reproduced subsystem failure at a time with a regression test or hardware trace. Preserve a known-working CIA locally for every accepted milestone.
4. Evaluate performance after stability gates pass. Prioritize measured hotspots in ARMv6 execution, texture conversion and PICA submission; do not assume a modern desktop emulator can simply be transplanted.

New 3DS offers substantially more CPU/cache headroom than Old 3DS, but both use ARM11 and PICA200 rather than a modern desktop GPU. Desktop JITs, Vulkan compute renderers and NEON-heavy code are reference material, not drop-in CTR backends. Native game ports remove emulation work, so their performance is not evidence of equivalent emulator speed.

No public release is scheduled by this branch. Publication of binaries is a separate maintainer decision after physical validation.
