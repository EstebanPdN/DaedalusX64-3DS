// Copyright (C) 2026 EstebanPdN
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <atomic>
#include <stddef.h>
#include <new>
#include <algorithm>

// One producer, one consumer. Reset/destruction require both to be quiescent.
template<class T> class SpscRing
{
public:
    explicit SpscRing(size_t capacity)
        : data(capacity > 1 ? new (std::nothrow) T[capacity] : nullptr), capacity(capacity), read(0), write(0) {}
    ~SpscRing() { delete[] data; }
    SpscRing(const SpscRing &) = delete;
    SpscRing &operator=(const SpscRing &) = delete;
    bool IsValid() const { return data != nullptr; }
    void Reset() { read.store(0); write.store(0); }
    size_t Size() const
    {
        if (!data) return 0;
        const size_t r = read.load(std::memory_order_acquire);
        const size_t w = write.load(std::memory_order_acquire);
        return w >= r ? w-r : capacity-r+w;
    }
    size_t Write(const T *source, size_t count)
    {
        if (!data || !source) return 0;
        const size_t w = write.load(std::memory_order_relaxed);
        const size_t r = read.load(std::memory_order_acquire);
        const size_t free = r > w ? r-w-1 : capacity-w+r-1;
        count = std::min(count, free);
        const size_t first = std::min(count, capacity-w);
        std::copy_n(source, first, data+w);
        std::copy_n(source+first, count-first, data);
        write.store((w+count) % capacity, std::memory_order_release);
        return count;
    }
    size_t Read(T *destination, size_t count)
    {
        if (!data || !destination) return 0;
        const size_t r = read.load(std::memory_order_relaxed);
        const size_t w = write.load(std::memory_order_acquire);
        const size_t available = w >= r ? w-r : capacity-r+w;
        count = std::min(count, available);
        const size_t first = std::min(count, capacity-r);
        std::copy_n(data+r, first, destination);
        std::copy_n(data, count-first, destination+first);
        read.store((r+count) % capacity, std::memory_order_release);
        return count;
    }
private:
    T *data;
    size_t capacity;
    std::atomic<size_t> read, write;
};
