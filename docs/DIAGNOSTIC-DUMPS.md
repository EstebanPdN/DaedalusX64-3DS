# Manual diagnostic dumps (0.2)

Press **L + R + SELECT** during emulation or in the ROM selector. The order does not matter, but all three buttons must be held together. Release all three before requesting another dump. Once recognized, the chord's held buttons are masked from the N64 controller mapping until fully released. Buttons pressed before the full chord was recognized may already have reached the game.

The bottom screen displays **WRITING DUMP**, followed by **DUMP SAVED** or **DUMP FAILED**. A moving marker indicates ongoing writes. The game and audio pause during capture and then resume. A full memory dump can be large and take several seconds or longer on a slow SD card; keep the SD inserted during writing.

## Location and contents

Each capture creates a new directory:

```text
sdmc:/3ds/DaedalusX64-EPD/Dumps/dump-YYYYMMDD-HHMMSS-NNNN/
    info.txt
    status.txt
    top.bmp
    bottom.bmp
    cpu.txt
    guest-memory.txt
    rdram.bin
    rsp-dmem-imem.bin
    ... other guest memory banks ...
    process-memory/
        manifest.txt
        ADDRESS.bin
```

- `info.txt`: source revision, model family, audio and recompiler settings, memory usage, ROM name/header identity and VI count. Header CRCs are **not** a computed full-file ROM hash.
- `top.bmp` / `bottom.bmp`: native framebuffer captures converted to ordinary top-down BMP. Top can be 400×240 or 800×240; bottom is 320×240. Capture occurs before painting the status message. The image is the most recently submitted render-target content at the pause point, not a promise of an exact VI/frame correspondence.
- `cpu.txt`: guest CPU/FPU/control register bit patterns, PC, delay state and pending events, after the execution core returns.
- Guest `.bin` files: RDRAM, RSP memory, peripheral register banks and save/mempak RAM. Layout is native to the emulator; N64 RDRAM byte addressing uses offset XOR 3 on this little-endian build. These files are diagnostic data, **not loadable save states**.
- `process-memory/`: readable code/private/continuous application mappings, including ordinary/linear heap and stack mappings when eligible. The manifest records virtual addresses, sizes, permissions and omitted mappings. Kernel, MMIO, VRAM, service/shared regions and aliases are excluded.
- `status.txt`: `COMPLETE` only when requested sections and their writes succeeded; otherwise `PARTIAL`. Missing status also means the dump was not confirmed complete. A failed blob is removed and recorded as failed in the corresponding manifest. Keep the whole directory, including partial results, when investigating failures.

ROM-selector captures omit the guest CPU/bank files and identify their context in `info.txt`. Process memory may still contain data from a previously closed game.

## Synchronization and limits

Gameplay input only requests a diagnostic stop. A dedicated CPU job returns through the normal execution-core exit path before the main loop performs file I/O; normal stop/exit requests take priority. The asynchronous audio task is allowed to finish. A nonblocking callback gate pauses NDSP refill work and waits for any in-flight callback; playback is paused while copying. GPU submissions are drained, and the limiter is reset after capture so SD writing time does not become a normal frame-time sample.

The host memory image is **sequential, not atomic**. Diagnostics, stdio, active stacks and platform service threads can change during writing. It is not a kernel dump or a replacement for a Luma exception dump. A completely hung CPU/GPU/audio worker that cannot reach the pause point may prevent a manual dump. There is no forced memory scraping from an unresponsive thread.

No dump is uploaded automatically. Memory captures can contain ROM fragments, save data and local filenames. Copy the relevant dump folder from the SD when investigating it; it is excluded from repository publication.

## Validation

Host tests exercise the production chord latch, callback-quiescence gate, memory-range exclusion, all output pixels for 320/400/800-wide BMPs, complete binary writes and write/flush/close/cancellation failures. Run `scripts/test_host.sh`; optional ThreadSanitizer run: `SANITIZERS=thread scripts/test_host.sh`.

Physical-console acceptance remains required: repeated chords, held chords, release/repress, native and wide screenshots, asynchronous audio before/after capture, low/full SD, HOME/lid behavior, and normal save/menu exit immediately after a dump. Retain the matching ELF/map and source revision with each report.
