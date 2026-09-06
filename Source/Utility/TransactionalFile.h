// Copyright (C) 2026 EstebanPdN
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdio.h>
#include <errno.h>
#include <stdint.h>
#include <string>

namespace SaveFile
{
// Injectable operations exercise the actual production transaction on the host.
struct Operations
{
    FILE *(*open)(const char *, const char *) = fopen;
    size_t (*write)(const void *, size_t, size_t, FILE *) = fwrite;
    int (*flush)(FILE *) = fflush;
    int (*close)(FILE *) = fclose;
    int (*move)(const char *, const char *) = rename;
    int (*erase)(const char *) = remove;
};

inline bool Write(const char *path, const uint8_t *source, size_t size,
                  unsigned byte_twiddle = 0, const Operations &ops = Operations())
{
    if (!path || !*path || !source || !size || byte_twiddle > 3 || (size & 3)) return false;
    const std::string temporary = std::string(path) + ".tmp";
    const std::string backup = std::string(path) + ".bak";
    FILE *file = ops.open(temporary.c_str(), "wb");
    if (!file) return false;
    bool ok = true;
    uint8_t block[2048];
    for (size_t offset = 0; offset < size && ok;)
    {
        const size_t count = size - offset < sizeof(block) ? size - offset : sizeof(block);
        for (size_t i = 0; i < count; ++i) block[i ^ byte_twiddle] = source[offset + i];
        ok = ops.write(block, 1, count, file) == count;
        offset += count;
    }
    if (ok) ok = ops.flush(file) == 0;
    if (ops.close(file) != 0) ok = false;
    if (!ok) { ops.erase(temporary.c_str()); return false; }

    // Never discard the old save before a complete replacement has been closed.
    errno = 0;
    FILE *old = ops.open(path, "rb");
    const bool had_old = old != nullptr;
    if (old) { if (ops.close(old) != 0) return false; }
    else if (errno != ENOENT) return false;
    if (had_old)
    {
        if (ops.erase(backup.c_str()) != 0 && errno != ENOENT) return false;
        if (ops.move(path, backup.c_str()) != 0) return false;
    }
    if (ops.move(temporary.c_str(), path) == 0) return true;
    if (had_old) ops.move(backup.c_str(), path); // If rollback fails, .bak remains recoverable.
    return false;
}

inline FILE *OpenForRead(const char *path)
{
    FILE *file = fopen(path, "rb");
    if (!file && errno == ENOENT)
    {
        const std::string backup = std::string(path) + ".bak";
        file = fopen(backup.c_str(), "rb");
    }
    return file;
}
}
