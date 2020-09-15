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

/* lzw.c
 * Lempel-Ziv-Welch compression.
 */

#include <config.h>
#include <err.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "algorithm.h"

/*
POSIX just says that compress must use the adaptive Lempel-Ziv algorithm (LZW).
However multiple variants of LZW exist and the actual file format is not
specified at all in POSIX and there does not seem to exist any normative
description of the format used by historical implementations. The format and
algorithm used by all known implementations are therefore described below.

The first two bytes of the file are the magic values 0x1F and 0x9D.
The third byte contains the maximum number of bits per code as an integer in the
lower 5 bits and flags in the upper 3 bits. The only defined flag is 0x80 which
enables block compression. All but few ancient versions of compress set that
flag. The flags 0x20 and 0x40 are reserved and should always be cleared.

Any data after these three bytes contains the compressed input as follows:
The compressor internally maintains a dictionary of codes. Initially the
dictionary contains codes 0 through 255 representing exactly those bytes in the
input file. If the block compression flag is set, code 256 is used to clear the
dictionary (see below), otherwise code 256 is just a free code.

The compressor then reads the longest sequence of bytes that exists in the
dictionary from the input file and writes the code for that sequence into the
output file. It then adds a new code for that sequence followed by the next byte
from the input to the dictionary.

The codes written to the output are initially written as 9-bit sequences. When
enough codes are in the dictionary that the codes would no longer fit into the
current bitsize, the bitsize is increased by one as long as it is not increased
beyond the maximum bitsize. Whenever the bitsize is increased the output is
padded with zero bits such that the amount of bytes written with codes of the
current bitsize is a multiple of that bitsize. This padding does not serve any
purpose but is needed to be compatible with other implementations of compress.

When codes can no longer be added to the dictionary, because the bitsize would
need to be increased beyond the maximum bitsize, and the block compression flag
is set, the compressor may clear the dictionary by writing code 256 to the
output and then resetting the bitsize back to 9 bits. Whether and when the
dictionary is cleared in this case is a quality of implementation matter.
Typically implementations do this if they determine that the compression ratio
is decreasing.

A particular quirk is that when the maximum bitsize is 9 and a code would be
added to the dictionary but can't because the dictionary is full, codes will
then be written as sequences of 10 bits with the most significant bit being
always 0. This is certainly a bug in the original compress implementation, but
it needs to be handled specially to ensure compatibility.
*/

static bool lzwCheckLevel(int level);
static int lzwCompress(int input, int output, int maxbits,
        struct fileinfo* info);
static int lzwDecompress(int input, int output, struct fileinfo* info,
        const unsigned char* buffer, size_t bufferSize);
static bool lzwProbe(const unsigned char* buffer, size_t bufferSize);

const struct algorithm algoLzw = {
    .names = "lzw",
    .extensions = "Z,taz:tar",
    .defaultLevel = 16,
    .checkLevel = lzwCheckLevel,
    .compress = lzwCompress,
    .decompress = lzwDecompress,
    .probe = lzwProbe
};

#define MAGIC1 0x1F
#define MAGIC2 0x9D
#define CLEAR_CODE 256
#define FLAG_BLOCK_COMPRESS 0x80
#define CHECK_INTERVAL 5000
#define BUFFER_SIZE (4096 * 8)
#define DICT_OFFSET 257

static bool lzwCheckLevel(int level) {
    return 9 <= level && level <= 16;
}

static bool lzwProbe(const unsigned char* buffer, size_t bufferSize) {
    return bufferSize >= 3 && buffer[0] == MAGIC1 && buffer[1] == MAGIC2;
}

struct state {
    double ratio; // for compress only
    off_t inputBytes;
    off_t outputBytes;
    off_t checkOffset; // for compress only
    off_t bytesInGroup;
    size_t bufferOffset;
    size_t inputSize; // for decompress only
    unsigned char currentBits;
    unsigned char bitOffset;
    unsigned char buffer[BUFFER_SIZE];
};

static bool checkRatio(struct state* state);
static bool writeCode(int output, uint16_t code, struct state* state);
static bool writePadding(int output, struct state* state);

// The directory entries need to be hashed, so that they can be found again with
// acceptable performance.
struct HashDict {
    uint16_t code;
    uint16_t prev;
    unsigned char c;
};

// The size must be a prime to ensure that double hashing can always iterate
// over the whole dictionary if necessary.
#define HASHDICT_SIZE 131101

static inline size_t hash1(uint16_t prev, unsigned char c) {
    return prev ^ c << 9;
}

static inline size_t hash2(uint16_t prev, unsigned char c) {
    return HASHDICT_SIZE - 1 - (prev ^ c << 8);
}

static size_t findIndex(struct HashDict* dict, uint16_t prev, unsigned char c) {
    size_t index = hash1(prev, c);
    while (dict[index].code != 0) {
        if (dict[index].prev == prev && dict[index].c == c) {
            return index;
        }
        if (index < hash2(prev, c)) index += HASHDICT_SIZE;
        index -= hash2(prev, c);
    }
    return index;
}

static int lzwCompress(int input, int output, int maxbits,
        struct fileinfo* info) {
    struct state state;
    state.ratio = 0.0;
    state.inputBytes = 1;
    state.outputBytes = 3;
    state.checkOffset = CHECK_INTERVAL;
    state.bytesInGroup = 0;
    state.currentBits = 9;
    state.bitOffset = 0;
    state.buffer[0] = MAGIC1;
    state.buffer[1] = MAGIC2;
    state.buffer[2] = FLAG_BLOCK_COMPRESS | maxbits;
    state.bufferOffset = 3;

    unsigned char inputBuffer[BUFFER_SIZE];
    ssize_t amount = read(input, inputBuffer, BUFFER_SIZE);
    if (amount < 0) return RESULT_READ_ERROR;
    if (amount == 0) {
        if (writeAll(output, state.buffer, state.bufferOffset) < 0) {
            return RESULT_WRITE_ERROR;
        }
        info->ratio = -1.0;
        return RESULT_OK;
    }
    size_t inputSize = amount;

    struct HashDict* dict = calloc(HASHDICT_SIZE, sizeof(struct HashDict));
    if (!dict) err(1, "malloc");

    size_t dictEntries = 1 << maxbits;
    size_t nextFree = DICT_OFFSET;
    uint16_t currentSeq = inputBuffer[0];
    size_t inputOffset = 1;

    while (true) {
        if (inputOffset >= inputSize) {
            amount = read(input, inputBuffer, BUFFER_SIZE);
            if (amount < 0) {
                free(dict);
                return RESULT_READ_ERROR;
            }
            if (amount == 0) break;
            inputOffset = 0;
            inputSize = amount;
        }
        unsigned char c = inputBuffer[inputOffset++];
        state.inputBytes++;

        size_t index = findIndex(dict, currentSeq, c);
        if (dict[index].code != 0) {
            currentSeq = dict[index].code;
        } else {
            if (!writeCode(output, currentSeq, &state)) goto writeError;
            if (nextFree == 512 && maxbits == 9 && state.currentBits == 9) {
                if (!writePadding(output, &state)) goto writeError;
                state.currentBits = 10;
            }
            if (nextFree < dictEntries) {
                dict[index].code = nextFree;
                dict[index].prev = currentSeq;
                dict[index].c = c;
                if ((nextFree & (nextFree - 1)) == 0) {
                    if (!writePadding(output, &state)) goto writeError;
                    state.currentBits++;
                }
                nextFree++;
            } else if (checkRatio(&state)) {
                if (!writeCode(output, CLEAR_CODE, &state) ||
                        !writePadding(output, &state)) {
                    goto writeError;
                }
                memset(dict, 0, HASHDICT_SIZE * sizeof(struct HashDict));
                nextFree = DICT_OFFSET;
                state.currentBits = 9;
            }
            currentSeq = c;
        }
    }
    free(dict);

    if (!writeCode(output, currentSeq, &state)) return RESULT_WRITE_ERROR;
    if (state.bitOffset) {
        state.outputBytes++;
        state.bufferOffset++;
    }

    if (writeAll(output, state.buffer, state.bufferOffset) < 0) {
        return RESULT_WRITE_ERROR;
    }
    info->ratio = 1.0 - (double) state.outputBytes / (double) state.inputBytes;
    return RESULT_OK;

writeError:
    free(dict);
    return RESULT_WRITE_ERROR;
}

static bool checkRatio(struct state* state) {
    if (state->inputBytes < state->checkOffset) return false;
    state->checkOffset = state->inputBytes + CHECK_INTERVAL;
    double ratio = (double) state->inputBytes / (double) state->outputBytes;
    if (ratio >= state->ratio) {
        state->ratio = ratio;
        return false;
    } else {
        state->ratio = 0.0;
        return true;
    }
}

static bool writeCode(int output, uint16_t code, struct state* state) {
    size_t bits = state->currentBits;
    if (state->bitOffset > 0) {
        state->buffer[state->bufferOffset++] |= code << state->bitOffset;
        code >>= 8 - state->bitOffset;
        bits -= 8 - state->bitOffset;
        state->bytesInGroup++;
        state->outputBytes++;
    }
    if (state->bufferOffset > BUFFER_SIZE - 2) {
        if (writeAll(output, state->buffer, state->bufferOffset) < 0) {
            return false;
        }
        state->bufferOffset = 0;
    }
    while (bits >= 8) {
        state->buffer[state->bufferOffset++] = code;
        code >>= 8;
        bits -= 8;
        state->bytesInGroup++;
        state->outputBytes++;
    }
    if (bits) {
        state->buffer[state->bufferOffset] = code;
    }
    state->bitOffset = bits;
    return true;
}

static bool writePadding(int output, struct state* state) {
    if (state->bitOffset) {
        state->bitOffset = 0;
        state->bytesInGroup++;
        state->outputBytes++;
        state->bufferOffset++;
    }
    size_t misalignment = state->bytesInGroup % state->currentBits;
    state->bytesInGroup = 0;
    if (!misalignment) return true;
    size_t padding = state->currentBits - misalignment;
    if (writeAll(output, state->buffer, state->bufferOffset) < 0) return false;

    state->bufferOffset = 0;
    size_t zeroes[16] = {0};
    if (writeAll(output, zeroes, padding) < 0) return false;
    state->outputBytes += padding;
    return true;
}

struct dict {
    uint16_t prev;
    unsigned char c;
    // We use this padding as a convenient buffer during decompression.
    unsigned char buffer;
};

static int readBuffer(int input, struct state* state);
static int readCode(int input, uint16_t* code, struct state* state);
static int discardPadding(int input, struct state* state);

static int lzwDecompress(int input, int output, struct fileinfo* info,
        const unsigned char* buffer, size_t bufferSize) {
    struct state state;
    state.inputBytes = 3;
    state.outputBytes = 0;
    state.bytesInGroup = 0;
    state.bufferOffset = 3;
    state.currentBits = 9;
    state.bitOffset = 0;

    memcpy(state.buffer, buffer, bufferSize);
    state.inputSize = bufferSize;

    while (state.inputSize < 3) {
        ssize_t amount = read(input, state.buffer,
                BUFFER_SIZE - state.inputSize);
        if (amount < 0) return RESULT_READ_ERROR;
        if (amount == 0) return RESULT_FORMAT_ERROR;
        state.inputSize += amount;
    }

    if (state.buffer[0] != MAGIC1 || state.buffer[1] != MAGIC2) {
        return RESULT_FORMAT_ERROR;
    }
    unsigned char maxbits = state.buffer[2] & 0x1F;
    if (maxbits < 9 || maxbits > 16) return RESULT_FORMAT_ERROR;
    if (state.buffer[2] & 0x60) return RESULT_FORMAT_ERROR;
    bool blockCompress = state.buffer[2] & FLAG_BLOCK_COMPRESS;
    size_t dictEntries = 1 << maxbits;
    size_t dictOffset = blockCompress ? DICT_OFFSET : DICT_OFFSET - 1;

    size_t nextFree = dictOffset;
    unsigned char outputBuffer[BUFFER_SIZE];
    size_t outputOffset = 0;
    uint16_t previousSeq;
    int readStatus = readCode(input, &previousSeq, &state);
    if (readStatus == -1) return RESULT_READ_ERROR;
    if (readStatus == 0) return RESULT_OK;
    if (previousSeq >= nextFree) return RESULT_FORMAT_ERROR;
    outputBuffer[outputOffset++] = previousSeq;
    state.outputBytes++;

    struct dict* dict = malloc((dictEntries - dictOffset) *
            sizeof(struct dict));
    if (!dict) err(1, "malloc");

    while (true) {
        uint16_t code;
        readStatus = readCode(input, &code, &state);
        if (readStatus == -1) goto readError;
        if (readStatus == 0) break;
        if (code > nextFree) goto formatError;
        if (blockCompress && code == CLEAR_CODE) {
            readStatus = discardPadding(input, &state);
            if (readStatus == -1) goto readError;
            if (readStatus == 0) goto formatError;
            nextFree = dictOffset;
            state.currentBits = 9;
            readStatus = readCode(input, &previousSeq, &state);
            if (readStatus == -1) goto readError;
            if (readStatus == 0) break;
            if (previousSeq >= nextFree) goto formatError;
            outputBuffer[outputOffset++] = previousSeq;
            state.outputBytes++;
            if (outputOffset >= BUFFER_SIZE) {
                if (writeAll(output, outputBuffer, BUFFER_SIZE) < 0) {
                    goto writeError;
                }
                outputOffset = 0;
            }
        } else {
            uint16_t originalCode = code;
            if (code == nextFree) code = previousSeq;

            size_t codeLength = 0;
            while (code > 0xFF) {
                // This cannot overflow because each code can only use codes
                // already in the dictionary and cycles are not allowed.
                dict[codeLength++].buffer = dict[code - dictOffset].c;
                code = dict[code - dictOffset].prev;
            }
            outputBuffer[outputOffset++] = code;
            state.outputBytes++;
            if (outputOffset >= BUFFER_SIZE) {
                if (writeAll(output, outputBuffer, BUFFER_SIZE) < 0) {
                    goto writeError;
                }
                outputOffset = 0;
            }

            for (size_t i = codeLength; i-- > 0;) {
                outputBuffer[outputOffset++] = dict[i].buffer;
                state.outputBytes++;
                if (outputOffset >= BUFFER_SIZE) {
                    if (writeAll(output, outputBuffer, BUFFER_SIZE) < 0) {
                        goto writeError;
                    }
                    outputOffset = 0;
                }
            }

            if (originalCode == nextFree) {
                outputBuffer[outputOffset++] = code;
                state.outputBytes++;
                if (outputOffset >= BUFFER_SIZE) {
                    if (writeAll(output, outputBuffer, BUFFER_SIZE) < 0) {
                        goto writeError;
                    }
                    outputOffset = 0;
                }
            }

            if (nextFree < dictEntries) {
                dict[nextFree - dictOffset].c = code;
                dict[nextFree - dictOffset].prev = previousSeq;
                nextFree++;
                if ((state.currentBits < maxbits || state.currentBits == 9) &&
                        (nextFree & (nextFree - 1)) == 0) {
                    readStatus = discardPadding(input, &state);
                    if (readStatus == -1) goto readError;
                    if (readStatus == 0) goto formatError;
                    state.currentBits++;
                }
            }
            previousSeq = originalCode;
        }
    }
    free(dict);
    if (writeAll(output, outputBuffer, outputOffset) < 0) {
        return RESULT_WRITE_ERROR;
    }

    info->ratio = 1.0 - (double) state.inputBytes / (double) state.outputBytes;
    return RESULT_OK;

formatError:
    free(dict);
    return RESULT_FORMAT_ERROR;
readError:
    free(dict);
    return RESULT_READ_ERROR;
writeError:
    free(dict);
    return RESULT_WRITE_ERROR;
}

static int readBuffer(int input, struct state* state) {
    if (state->bufferOffset >= state->inputSize) {
        ssize_t amount = read(input, state->buffer, BUFFER_SIZE);
        if (amount < 0) return -1;
        state->bufferOffset = 0;
        state->inputSize = amount;
        if (amount == 0) return 0;
    }
    return 1;
}

static int readCode(int input, uint16_t* code, struct state* state) {
    size_t bits = state->currentBits;
    *code = 0;
    if (state->bitOffset > 0) {
        *code = state->buffer[state->bufferOffset++] >> state->bitOffset;
        bits -= 8 - state->bitOffset;
        state->bytesInGroup++;
        state->inputBytes++;
    }

    while (bits >= 8) {
        int readStatus = readBuffer(input, state);
        if (readStatus != 1) return readStatus;
        *code |= (uint16_t) state->buffer[state->bufferOffset++] <<
                (state->currentBits - bits);
        bits -= 8;
        state->bytesInGroup++;
        state->inputBytes++;
    }
    if (bits) {
        int readStatus = readBuffer(input, state);
        if (readStatus != 1) return readStatus;
        *code |= ((uint16_t) state->buffer[state->bufferOffset] &
                ((1 << bits) - 1)) << (state->currentBits - bits);
    }
    state->bitOffset = bits;
    return 1;
}

static int discardPadding(int input, struct state* state) {
    if (state->bitOffset) {
        state->bitOffset = 0;
        state->bytesInGroup++;
        state->inputBytes++;
        state->bufferOffset++;
    }
    size_t misalignment = state->bytesInGroup % state->currentBits;
    state->bytesInGroup = 0;
    if (!misalignment) return 1;
    size_t padding = state->currentBits - misalignment;
    state->inputBytes += padding;
    state->bufferOffset += padding;
    while (state->bufferOffset >= state->inputSize) {
        size_t remaining = state->bufferOffset - state->inputSize;
        int readStatus = readBuffer(input, state);
        if (readStatus != 1) return readStatus;
        state->bufferOffset = remaining;
    }
    return 1;
}
