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

/* xz.c
 * XZ compression.
 */

#include <config.h>
#include <string.h>
#include <unistd.h>
#include "algorithm.h"

#if WITH_LIBLZMA
#  include <lzma.h>
#endif

static int xzCompress(int input, int output, int level, struct fileinfo* info);
static int xzDecompress(int input, int output, struct fileinfo* info,
        const unsigned char* buffer, size_t bufferSize);
static bool xzProbe(const unsigned char* buffer, size_t bufferSize);

const struct algorithm algoXz = {
    .names = "xz",
    .extensions = "xz,txz:tar",
    .minLevel = 0,
    .defaultLevel = 6,
    .maxLevel = 9,
    .compress = xzCompress,
    .decompress = xzDecompress,
    .probe = xzProbe
};

#define BUFFER_SIZE (4096 * 8)
#define XZMAGIC "\xfd""7zXZ"

static bool xzProbe(const unsigned char* buffer, size_t bufferSize) {
    return bufferSize >= 6 && memcmp(buffer, XZMAGIC, 6) == 0;
}

static int xzCompress(int input, int output, int level, struct fileinfo* info) {
#if WITH_LIBLZMA
    lzma_stream stream = LZMA_STREAM_INIT;
    lzma_ret status = lzma_easy_encoder(&stream, level, LZMA_CHECK_CRC64);
    if (status == LZMA_MEM_ERROR) return RESULT_OUT_OF_MEMORY;
    if (status != LZMA_OK) return RESULT_UNKNOWN_ERROR;

    unsigned char inputBuffer[BUFFER_SIZE];
    unsigned char outputBuffer[BUFFER_SIZE];
    stream.next_out = outputBuffer;
    stream.avail_out = sizeof(outputBuffer);

    while (true) {
        if (stream.avail_out == 0) {
            if (writeAll(output, outputBuffer, sizeof(outputBuffer)) < 0) {
                lzma_end(&stream);
                return RESULT_WRITE_ERROR;
            }
            stream.next_out = outputBuffer;
            stream.avail_out = sizeof(outputBuffer);
        }

        if (stream.avail_in == 0) {
            ssize_t bytesRead = read(input, inputBuffer, sizeof(inputBuffer));
            if (bytesRead < 0) {
                lzma_end(&stream);
                return RESULT_READ_ERROR;
            }
            stream.next_in = inputBuffer;
            stream.avail_in = bytesRead;
            if (bytesRead == 0) break;
        }

        status = lzma_code(&stream, LZMA_RUN);
        if (status != LZMA_OK) {
            lzma_end(&stream);
            // LZMA_DATA_ERROR means the data exceeds the maximum possible size.
            return status == LZMA_MEM_ERROR ? RESULT_OUT_OF_MEMORY :
                    status == LZMA_DATA_ERROR ? RESULT_FORMAT_ERROR :
                    RESULT_UNKNOWN_ERROR;
        }
    }

    do {
        status = lzma_code(&stream, LZMA_FINISH);
        if (status != LZMA_OK && status != LZMA_STREAM_END) {
            lzma_end(&stream);
            return status == LZMA_MEM_ERROR ? RESULT_OUT_OF_MEMORY :
                    status == LZMA_DATA_ERROR ? RESULT_FORMAT_ERROR :
                    RESULT_UNKNOWN_ERROR;
        }
        if (writeAll(output, outputBuffer,
                sizeof(outputBuffer) - stream.avail_out) < 0) {
            lzma_end(&stream);
            return RESULT_WRITE_ERROR;
        }
        stream.next_out = outputBuffer;
        stream.avail_out = sizeof(outputBuffer);
    } while (status != LZMA_STREAM_END);

    info->uncompressedSize = stream.total_in;
    info->compressedSize = stream.total_out;
    lzma_end(&stream);
    return RESULT_OK;
#else
    (void) input; (void) output; (void) level; (void) info;
    return RESULT_UNIMPLEMENTED_FORMAT;
#endif
}

static int xzDecompress(int input, int output, struct fileinfo* info,
        const unsigned char* buffer, size_t bufferSize) {
#if WITH_LIBLZMA
    if (output == -2) {
        output = openOutputFile(NULL, info->oinfo);
        if (output < 0) return RESULT_OPEN_FAILURE;
    }

    lzma_stream stream = LZMA_STREAM_INIT;
    lzma_ret status = lzma_stream_decoder(&stream, UINT64_MAX,
            LZMA_CONCATENATED);
    if (status == LZMA_MEM_ERROR) return RESULT_OUT_OF_MEMORY;
    if (status != LZMA_OK) return RESULT_UNKNOWN_ERROR;

    unsigned char inputBuffer[BUFFER_SIZE];
    unsigned char outputBuffer[BUFFER_SIZE];
    memcpy(inputBuffer, buffer, bufferSize);

    stream.next_in = inputBuffer;
    stream.avail_in = bufferSize;
    stream.next_out = outputBuffer;
    stream.avail_out = sizeof(outputBuffer);

    while (true) {
        if (stream.avail_out == 0) {
            if (writeAll(output, outputBuffer, sizeof(outputBuffer)) < 0) {
                lzma_end(&stream);
                return RESULT_WRITE_ERROR;
            }
            stream.next_out = outputBuffer;
            stream.avail_out = sizeof(outputBuffer);
        }

        if (stream.avail_in == 0) {
            ssize_t bytesRead = read(input, inputBuffer, sizeof(inputBuffer));
            if (bytesRead < 0) {
                lzma_end(&stream);
                return RESULT_READ_ERROR;
            }
            stream.next_in = inputBuffer;
            stream.avail_in = bytesRead;
            if (bytesRead == 0) break;
        }

        status = lzma_code(&stream, LZMA_RUN);
        if (status != LZMA_OK) {
            lzma_end(&stream);
            return status == LZMA_MEM_ERROR ? RESULT_OUT_OF_MEMORY :
                    status == LZMA_FORMAT_ERROR ? RESULT_FORMAT_ERROR :
                    status == LZMA_DATA_ERROR ? RESULT_FORMAT_ERROR :
                    RESULT_UNKNOWN_ERROR;
        }
    }

    do {
        status = lzma_code(&stream, LZMA_FINISH);
        if (status != LZMA_OK && status != LZMA_STREAM_END) {
            lzma_end(&stream);
            return status == LZMA_MEM_ERROR ? RESULT_OUT_OF_MEMORY :
                    status == LZMA_FORMAT_ERROR ? RESULT_FORMAT_ERROR :
                    status == LZMA_DATA_ERROR ? RESULT_FORMAT_ERROR :
                    RESULT_UNKNOWN_ERROR;
        }
        if (writeAll(output, outputBuffer,
                sizeof(outputBuffer) - stream.avail_out) < 0) {
            lzma_end(&stream);
            return RESULT_WRITE_ERROR;
        }
        stream.next_out = outputBuffer;
        stream.avail_out = sizeof(outputBuffer);
    } while (status != LZMA_STREAM_END);

    info->compressedSize = stream.total_in;
    info->uncompressedSize = stream.total_out;
    info->crc = -1;
    lzma_end(&stream);

    return RESULT_OK;
#else
    (void) input; (void) output; (void) info; (void) buffer; (void) bufferSize;
    return RESULT_UNIMPLEMENTED_FORMAT;
#endif
}
