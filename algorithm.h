/* Copyright (c) 2020 Dennis Wölfing
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/* algorithm.h
 * Compression algorithms.
 */

#ifndef ALGORITHM_H
#define ALGORITHM_H

#include <stdbool.h>
#include <stdint.h>
#include <time.h>
#include <sys/types.h>

enum {
    RESULT_OK,
    RESULT_READ_ERROR,
    RESULT_WRITE_ERROR,
    RESULT_FORMAT_ERROR,
    RESULT_UNRECOGNIZED_FORMAT,
    RESULT_UNIMPLEMENTED_FORMAT,
    RESULT_OUT_OF_MEMORY,
    RESULT_OPEN_FAILURE,
    RESULT_UNKNOWN_ERROR
};

struct fileinfo {
    const char* name;
    struct timespec modificationTime;
    off_t compressedSize;
    off_t uncompressedSize;
    uint32_t crc;
    struct outputinfo* oinfo;
};

struct algorithm {
    // Names of this algorithm separated by commas.
    const char* names;
    // File extensions separated by commas. Entries using the format A:B will
    // cause extension A to be replaced by extension B when decompressing.
    const char* extensions;
    int minLevel;
    int defaultLevel;
    int maxLevel;
    int (*compress)(int input, int output, int level, struct fileinfo* info);
    int (*decompress)(int input, int output, struct fileinfo* info,
            const unsigned char* buffer, size_t bufferSize);
    bool (*probe)(const unsigned char* buffer, size_t bufferSize);
};

extern const struct algorithm algoDeflate;
extern const struct algorithm algoLzw;

int openOutputFile(const char* outputName, struct outputinfo* oinfo);
ssize_t writeAll(int fd, const void* buffer, size_t size);

#endif
