// Copyright (C) 2026 EstebanPdN
// SPDX-License-Identifier: GPL-2.0-or-later
// Retains the upstream linear interpolation and output-count convention.
#include "HLEAudio/AudioBuffer.h"
#include <string.h>
#ifdef __3DS__
#include <3ds.h>
#else
#include <thread>
#endif

CAudioBuffer::CAudioBuffer(u32 size) : mRing(size), mStopped(false) {}
CAudioBuffer::~CAudioBuffer() = default;
u32 CAudioBuffer::GetNumBufferedSamples() const { return mRing.Size(); }
void CAudioBuffer::Cancel() { mStopped.store(true, std::memory_order_release); }
void CAudioBuffer::Reset() { mRing.Reset(); mStopped.store(false, std::memory_order_release); }

void CAudioBuffer::AddSamples(const Sample *samples, u32 count, u32 frequency, u32 outputFrequency)
{
    if (!samples || count < 2 || !frequency || !outputFrequency || !mRing.IsValid()) return;
    const u64 scaledCount = u64(count) * outputFrequency / frequency;
    // Reject corrupt rates/lengths that would monopolize the emulation thread.
    if (scaledCount < 2 || scaledCount > 1024 * 1024) return;
    const u64 step = (u64(frequency) << 12) / outputFrequency;
    if (!step) return;
    u64 position = 0;
    Sample batch[64];
    u32 remaining = scaledCount - 1;
    while (remaining && !mStopped.load(std::memory_order_acquire))
    {
        u32 generated = 0;
        while (generated < 64 && remaining)
        {
            const u64 index = position >> 12;
            if (index + 1 >= count) { remaining = 0; break; }
            const s32 fraction = position & 4095;
            batch[generated].L = samples[index].L + ((s32(samples[index+1].L - samples[index].L) * fraction) >> 12);
            batch[generated].R = samples[index].R + ((s32(samples[index+1].R - samples[index].R) * fraction) >> 12);
            ++generated; --remaining; position += step;
        }
        u32 sent = 0, waits = 0;
        while (sent < generated)
        {
            sent += mRing.Write(batch + sent, generated - sent);
            if (sent == generated) break;
            if (mStopped.load(std::memory_order_acquire) || ++waits > 2000) return;
#ifdef __3DS__
            svcSleepThread(50000); // Yield while NDSP consumes; bounded if the device stops.
#else
            std::this_thread::yield();
#endif
        }
    }
}

u32 CAudioBuffer::Drain(Sample *samples, u32 count)
{
    if (!samples) return 0;
    const u32 written = mRing.Read(samples, count);
    memset(samples + written, 0, (count - written) * sizeof(Sample));
    return written;
}
