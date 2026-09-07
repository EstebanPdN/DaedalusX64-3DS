// Copyright (C) 2026 EstebanPdN
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <atomic>
#include <algorithm>

namespace CTRDump
{
// One capture until every member of the chord has been released.
class Chord
{
    uint32_t mask;
    bool latched = false;
public:
    explicit Chord(uint32_t buttons) : mask(buttons) {}
    bool Update(uint32_t held)
    {
        if (!(held & mask)) latched = false;
        if (!latched && (held & mask) == mask) { latched = true; return true; }
        return false;
    }
    uint32_t Filter(uint32_t held) const { return latched ? held & ~mask : held; }
};

// The callback never waits. The owner pauses and waits for in-flight work.
class CallbackGate
{
    std::atomic<bool> paused{false};
    std::atomic<unsigned> active{0};
public:
    bool Enter()
    {
        active.fetch_add(1);
        if (paused.load()) { active.fetch_sub(1); return false; }
        return true;
    }
    void Leave() { active.fetch_sub(1); }
    template<class Wait> void Pause(Wait wait)
    {
        paused.store(true);
        while (active.load()) wait();
    }
    void Resume() { paused.store(false); }
};

inline bool ApplicationRange(uint32_t base, uint32_t size, bool readable, bool owned)
{
    if (!readable || !owned || !size || base < 0x00100000 || base >= 0x40000000) return false;
    const uint64_t end = uint64_t(base) + size;
    // Exclude device, service/shared and VRAM address space even if misclassified.
    return end <= 0x1F000000 || (base >= 0x30000000 && end <= 0x40000000);
}

inline bool CloseChecked(FILE *file)
{
    if (!file) return false;
    bool ok = !ferror(file);
    if (fflush(file)) ok = false;
    if (fclose(file)) ok = false;
    return ok;
}

struct FileOperations
{
    FILE *(*open)(const char *, const char *) = fopen;
    size_t (*write)(const void *, size_t, size_t, FILE *) = fwrite;
    int (*flush)(FILE *) = fflush;
    int (*close)(FILE *) = fclose;
    int (*erase)(const char *) = remove;
};
using Progress = bool (*)(size_t);
inline bool WriteBlob(const char *path, const void *data, size_t size,
                      Progress progress = nullptr, const FileOperations &ops = FileOperations())
{
    if (!path || !data || !size) return false;
    FILE *file = ops.open(path, "wb");
    if (!file) return false;
    bool ok = true;
    uint8_t block[16 * 1024];
    for (size_t offset = 0; offset < size;)
    {
        size_t count = std::min<size_t>(sizeof(block), size-offset);
        // A process dump may include stdio's own objects or this active stack.
        // Snapshot a chunk before fwrite mutates stdio; memmove permits stack overlap.
        memmove(block, static_cast<const uint8_t *>(data)+offset, count);
        if (ops.write(block, 1, count, file) != count) { ok=false; break; }
        offset += count;
        if (progress && !progress(count)) { ok=false; break; }
    }
    if (ops.flush(file)) ok=false;
    if (ops.close(file)) ok=false;
    if (!ok) ops.erase(path);
    return ok;
}

// Native 3DS RGB8 is column-major BGR, with the vertical axis reversed.
// Write a normal, top-down BMP using only one small row of scratch memory.
inline bool WriteFramebufferBmp(const char *path, const uint8_t *pixels,
                                unsigned physicalWidth, unsigned physicalHeight)
{
    if (!pixels || physicalWidth != 240 ||
        (physicalHeight != 320 && physicalHeight != 400 && physicalHeight != 800)) return false;
    uint8_t header[54] = {};
    auto put16 = [&](unsigned p, uint16_t v) { header[p]=v; header[p+1]=v>>8; };
    auto put32 = [&](unsigned p, uint32_t v) { for (unsigned i=0;i<4;++i) header[p+i]=v>>(8*i); };
    const unsigned width=physicalHeight, height=physicalWidth, stride=(width*3+3)&~3u;
    header[0]='B';header[1]='M';put32(2,54+stride*height);put32(10,54);put32(14,40);
    put32(18,width);put32(22,uint32_t(-int32_t(height)));put16(26,1);put16(28,24);put32(34,stride*height);
    FILE *file=fopen(path,"wb"); if(!file) return false;
    bool ok=fwrite(header,1,sizeof(header),file)==sizeof(header);
    uint8_t row[800*3] = {};
    for(unsigned y=0;y<height && ok;++y)
    {
        for(unsigned x=0;x<width;++x) memcpy(row+x*3,pixels+((x*height)+(height-1-y))*3,3);
        ok=fwrite(row,1,stride,file)==stride;
    }
    if(!CloseChecked(file)) ok=false;
    if(!ok) remove(path);
    return ok;
}
}
