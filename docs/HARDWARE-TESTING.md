# Hardware validation protocol

Status at initial source publication: **not yet run on physical hardware**. No game has been assigned a new compatibility rating on the basis of this patch.

Record date, exact console model, system/CFW version, launch format, source SHA, CIA SHA-256, ROM region/hash, audio mode and all changed settings. Keep the original build and a separate copy of all saves for comparison. Use the same ROM and settings in both builds.

| Test | Procedure | Acceptance |
| --- | --- | --- |
| Startup | Launch CIA, then 3DSX; test both a populated and empty Roms folder | Readable UI; empty list explains the path and exits; no exception |
| Failure handling | Missing DSP firmware; invalid ROM; insufficient writable SD access | Clear failure message and clean exit/return where supported |
| Normal emulation | Play a repeatable 10-minute route; include menus, gameplay and transitions | No new crash; correct game speed and acceptable audio compared with baseline |
| Open/close | Open and leave a game 20 times, alternating two titles | No growing failure rate, stale audio or retained game state |
| Persistence | Save in-game, exit normally, relaunch and load; repeat with native save and mempak titles | Latest successful save reloads; previous `.bak` remains readable |
| Suspension | Close/open lid during gameplay and menus; use HOME/resume/close repeatedly | Resumes or closes correctly; no deadlock or continuing audio after exit |
| Audio | Repeat with disabled, synchronous and asynchronous settings | No queue stalls; compare underruns, audio and real-time speed |
| Memory pressure | Include a large-ROM title and repeated texture-heavy transitions | No new crash or corrupt display; record any reproducible limit |
| Long session | Run for at least one hour after short tests pass | No progressive corruption or unexplained deterioration |

Run the matrix on a physical New 3DS/New 2DS XL and a physical Old 3DS/2DS. Reducing New 3DS clock speed is not an adequate Old 3DS substitute because memory and cache differ. Emulator-only testing cannot validate real DSP/GPU service behavior or performance.

For a failure, retain the crash dump, exact local ELF/map, reproduction steps and whether the original build fails too. Do not label an upstream issue fixed until its reproduction passes on the affected model. A 30-frame/s game at correct speed is a different target from a 60-frame/s game; preserve PAL/NTSC behavior.
