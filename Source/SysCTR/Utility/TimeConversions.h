// Copyright (C) 2026 EstebanPdN
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdint.h>

namespace CTRTime
{
constexpr uint64_t TicksPerSecond = 268123480ULL;
constexpr uint64_t MillisecondsToNanoseconds(uint32_t ms)
{
    return uint64_t(ms) * 1000000ULL;
}
constexpr uint64_t TicksToNanoseconds(uint32_t ticks)
{
    return uint64_t(ticks) * 1000000000ULL / TicksPerSecond;
}
constexpr uint64_t TicksToMilliseconds(uint64_t ticks)
{
    return (ticks / TicksPerSecond) * 1000ULL
         + (ticks % TicksPerSecond) * 1000ULL / TicksPerSecond;
}
}
