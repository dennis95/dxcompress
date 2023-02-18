/* Copyright (c) 2020, 2023 Dennis Wölfing
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

/* deflate.c
 * Deflate compression.
 */

#include <config.h>
#include <string.h>
#include "algorithm.h"

#if WITH_ZLIB
#  include <zlib.h>
#endif

static int gzipCompress(int input, int output, int level,
        struct fileinfo* info);
static int gzipDecompress(int input, int output, struct fileinfo* info,
        const unsigned char* buffer, size_t bufferSize);
static bool gzipProbe(const unsigned char* buffer, size_t bufferSize);

const struct algorithm algoDeflate = {
    .names = "deflate,gzip",
    .extensions = "gz,tgz:tar",
    .minLevel = 1,
    .defaultLevel = 6,
    .maxLevel = 9,
    .compress = gzipCompress,
    .decompress = gzipDecompress,
    .probe = gzipProbe
};

#define MAGIC1 0x1F
#define MAGIC2 0x8B
#define BUFFER_SIZE (4096 * 8)

static bool gzipProbe(const unsigned char* buffer, size_t bufferSize) {
    return bufferSize >= 6 && buffer[0] == MAGIC1 && buffer[1] == MAGIC2;
}

static int gzipCompress(int input, int output, int level,
        struct fileinfo* info) {
#if WITH_ZLIB
    z_stream stream;
    stream.zalloc = Z_NULL;
    stream.zfree = Z_NULL;
    int status = deflateInit2(&stream, level, Z_DEFLATED, 15 + 16, 8,
            Z_DEFAULT_STRATEGY);
    if (status == Z_MEM_ERROR) return RESULT_OUT_OF_MEMORY;
    if (status != Z_OK) return RESULT_UNKNOWN_ERROR;

    // Store metadata in the gzip header.
    gz_header header;
    header.text = 0;
    time_t time = info->modificationTime.tv_sec;
    if (time < 0 || time > 0xFFFFFFFF) {
        header.time = 0;
    } else {
        header.time = time;
    }
    header.os = 3; // Unix
    header.extra = Z_NULL;
    header.name = (Bytef*) info->name;
    header.comment = Z_NULL;
    header.hcrc = 0;
    deflateSetHeader(&stream, &header);

    unsigned char inputBuffer[BUFFER_SIZE];
    unsigned char outputBuffer[BUFFER_SIZE];
    stream.next_out = outputBuffer;
    stream.avail_in = 0;
    stream.avail_out = sizeof(outputBuffer);

    while (true) {
        if (stream.avail_out == 0) {
            if (writeAll(output, outputBuffer, sizeof(outputBuffer)) < 0) {
                deflateEnd(&stream);
                return RESULT_WRITE_ERROR;
            }
            stream.next_out = outputBuffer;
            stream.avail_out = sizeof(outputBuffer);
        }

        if (stream.avail_in == 0) {
            ssize_t bytesRead = read(input, inputBuffer, sizeof(inputBuffer));
            if (bytesRead < 0) {
                deflateEnd(&stream);
                return RESULT_READ_ERROR;
            }
            stream.next_in = inputBuffer;
            stream.avail_in = bytesRead;
            if (bytesRead == 0) break;
        }

        deflate(&stream, Z_NO_FLUSH);
    }

    do {
        status = deflate(&stream, Z_FINISH);
        if (writeAll(output, outputBuffer,
                sizeof(outputBuffer) - stream.avail_out) < 0) {
            deflateEnd(&stream);
            return RESULT_WRITE_ERROR;
        }
        stream.next_out = outputBuffer;
        stream.avail_out = sizeof(outputBuffer);
    } while (status != Z_STREAM_END);

    info->uncompressedSize = stream.total_in;
    info->compressedSize = stream.total_out;
    deflateEnd(&stream);
    return RESULT_OK;
#else
    (void) input; (void) output; (void) level; (void) info;
    return RESULT_UNIMPLEMENTED_FORMAT;
#endif
}

static int gzipDecompress(int input, int output, struct fileinfo* info,
        const unsigned char* buffer, size_t bufferSize) {
#if WITH_ZLIB
    unsigned char inputBuffer[BUFFER_SIZE];
    memcpy(inputBuffer, buffer, bufferSize);
    ssize_t bytesRead = read(input, inputBuffer + bufferSize,
            sizeof(inputBuffer) - bufferSize);
    if (bytesRead < 0) return RESULT_READ_ERROR;

    z_stream stream;
    stream.zalloc = Z_NULL;
    stream.zfree = Z_NULL;
    stream.next_in = inputBuffer;
    stream.avail_in = bufferSize + bytesRead;
    int status = inflateInit2(&stream, 15 + 16);
    if (status == Z_MEM_ERROR) return RESULT_OUT_OF_MEMORY;
    if (status != Z_OK) return RESULT_UNKNOWN_ERROR;

    gz_header header;
#ifndef NAME_MAX
#  define NAME_MAX 1024
#endif
    Bytef name[NAME_MAX];
    header.name = name;
    header.name_max = sizeof(name);
    header.extra = Z_NULL;
    header.comment = Z_NULL;
    inflateGetHeader(&stream, &header);

    unsigned char outputBuffer[BUFFER_SIZE];
    stream.next_out = outputBuffer;
    stream.avail_out = sizeof(outputBuffer);

    bool endOfStream = false;

    info->compressedSize = bufferSize + bytesRead;
    info->uncompressedSize = 0;

    while (true) {
        if (output == -2 && header.done == 1) {
            output = openOutputFile((const char*) header.name, info->oinfo);
            if (output < 0) {
                inflateEnd(&stream);
                return RESULT_OPEN_FAILURE;
            }
        }

        if (stream.avail_out == 0 && output != -2) {
            if (writeAll(output, outputBuffer, sizeof(outputBuffer)) < 0) {
                inflateEnd(&stream);
                return RESULT_WRITE_ERROR;
            }
            info->uncompressedSize += sizeof(outputBuffer);
            stream.next_out = outputBuffer;
            stream.avail_out = sizeof(outputBuffer);
        }

        if (stream.avail_in == 0) {
            ssize_t bytesRead = read(input, inputBuffer, sizeof(inputBuffer));
            if (bytesRead < 0) {
                inflateEnd(&stream);
                return RESULT_READ_ERROR;
            }
            info->compressedSize += bytesRead;
            stream.next_in = inputBuffer;
            stream.avail_in = bytesRead;
        }

        if (endOfStream) {
            if (stream.avail_in == 0) break;
            // Reset decompression to support concatenated gzipped files.
            inflateReset(&stream);
            endOfStream = false;
        }

        if (stream.avail_in == 0) {
            inflateEnd(&stream);
            return RESULT_FORMAT_ERROR;
        }

        status = inflate(&stream, Z_NO_FLUSH);
        if (status == Z_STREAM_END) {
            endOfStream = true;
        } else if (status != Z_OK) {
            inflateEnd(&stream);
            return status == Z_DATA_ERROR ? RESULT_FORMAT_ERROR :
                    status == Z_MEM_ERROR ? RESULT_OUT_OF_MEMORY :
                    RESULT_UNKNOWN_ERROR;
        }
    }

    if (writeAll(output, outputBuffer,
            sizeof(outputBuffer) - stream.avail_out) < 0) {
        inflateEnd(&stream);
        return RESULT_WRITE_ERROR;
    }
    info->uncompressedSize += sizeof(outputBuffer) - stream.avail_out;
    info->modificationTime.tv_sec = header.time;
    info->modificationTime.tv_nsec = 0;
    info->crc = stream.adler;
    if (header.name) {
        info->name = strndup((const char*) header.name, header.name_max);
    }
    inflateEnd(&stream);

    return RESULT_OK;
#else
    (void) input; (void) output; (void) info; (void) buffer; (void) bufferSize;
    return RESULT_UNIMPLEMENTED_FORMAT;
#endif
}
