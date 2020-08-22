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

/* main.c
 * Compression and decompression utility.
 */

#include <config.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include "algorithm.h"

static const struct algorithm* algorithms[] = {
    // LZW must be the first entry in this list.
    &algoLzw,
    NULL
};

static const struct algorithm* getAlgorithm(const char* name);
static bool getConfirmation(const char* filename);
static const struct algorithm* handleExtensions(const char* filename,
        const char** inputName, const char** outputName);
static const struct algorithm* probe(int input, unsigned char* buffer,
        size_t* bufferUsed, size_t bufferSize);
static int processOperand(const char* filename);

static const struct algorithm* algorithm;
static unsigned long bits = 16;
static bool decompress = false;
static bool force = false;
static bool verbose = false;
static bool writeToStdout = false;
static char* allocatedName;

int main(int argc, char* argv[]) {
    struct option longopts[] = {
        { "decompress", no_argument, 0, 'd' },
        { "force", no_argument, 0, 'f' },
        { "help", no_argument, 0, 'h' },
        { "stdout", no_argument, 0, 'c' },
        { "to-stdout", no_argument, 0, 'c' },
        { "uncompress", no_argument, 0, 'd' },
        { "verbose", no_argument, 0, 'v' },
        { "version", no_argument, 0, 'V' },
        { 0, 0, 0, 0 }
    };

    const char* algorithmName = "lzw";

    int c;
    while ((c = getopt_long(argc, argv, "b:cdfhm:vV", longopts, NULL)) != -1) {
        switch (c) {
        case 'b': {
            char* end;
            bits = strtoul(optarg, &end, 10);
            if (*end) err(1, "invalid bits value: '%s'", optarg);
        } break;
        case 'c':
            writeToStdout = true;
            break;
        case 'd':
            decompress = true;
            break;
        case 'f':
            force = true;
            break;
        case 'h':
            printf("Usage: %s [OPTIONS] [FILE...]\n"
"  -b BITS                  use at most BITS bits in a code\n"
"  -d, --decompress         decompress files\n"
"  -c, --stdout             write output to stdout\n"
"  -f, --force              force compression\n"
"  -h, --help               display this help\n"
"  -m ALGO                  use the ALGO algorithm for compression\n"
"  -v, --verbose            print filenames and compression ratios\n"
"  -V, --version            display version info\n",
argv[0]);
            return 0;
        case 'm':
            algorithmName = optarg;
            break;
        case 'v':
            verbose = true;
            break;
        case 'V':
            printf("%s (%s)\n", argv[0], PACKAGE_STRING);
            return 0;
        case '?':
            return 1;
        }
    }

    if (!decompress) {
        algorithm = getAlgorithm(algorithmName);
        if (!algorithm) {
            errx(1, "unknown compression algorithm '%s'", algorithmName);
        }
    }

    if (bits < 9 || bits > 16) {
        errx(1, "invalid bits value: '%lu'", bits);
    }

    int status = 0;
    if (optind >= argc) {
        status = processOperand("-");
    }

    for (int i = optind; i < argc; i++) {
        int result = processOperand(argv[i]);
        if (status != 1) status = result;
    }
    return status;
}

static const struct algorithm* getAlgorithm(const char* name) {
    size_t nameLength = strlen(name);
    for (size_t i = 0; algorithms[i]; i++) {
        const char* names = algorithms[i]->names;
        while (*names) {
            size_t length = strcspn(names, ",");
            if (length == nameLength && strncmp(name, names, length) == 0) {
                return algorithms[i];
            }
            names += length;
            if (*names) names++;
        }
    }
    return NULL;
}

static bool getConfirmation(const char* filename) {
    if (!isatty(0)) return false;
    fprintf(stderr, "File '%s' already exists, overwrite? ", filename);
    char response = fgetc(stdin);
    char c = response;
    while (c != '\n' && c != EOF) {
        c = fgetc(stdin);
    }
    return response == 'y' || response == 'Y';
}

static size_t reverse_strcspn(const char* s, const char* set) {
    const char* current = NULL;
    while (*set) {
        const char* p = strrchr(s, *set);
        if (p && (!current || p > current)) {
            current = p;
        }
        set++;
    }
    return current ? (size_t) (current - s) : strlen(s);
}

static const struct algorithm* handleExtensions(const char* filename,
        const char** inputName, const char** outputName) {
    size_t nameWithoutExtLength = reverse_strcspn(filename, ".-_");
    if (filename[nameWithoutExtLength]) {
        const char* extension = &filename[nameWithoutExtLength + 1];
        size_t extLength = strlen(extension);
        for (size_t i = 0; algorithms[i]; i++) {
            const char* extensions = algorithms[i]->extensions;
            while (*extensions) {
                size_t length = strcspn(extensions, ",:");
                if (length == extLength && strncmp(extension, extensions,
                        length) == 0) {
                    *inputName = filename;
                    if (extensions[length] == ':') {
                        const char* newExt = extensions + extLength + 1;
                        size_t newExtLength = strcspn(newExt, ",");
                        allocatedName = malloc(nameWithoutExtLength +
                                newExtLength + 2);
                        if (!allocatedName) err(1, "malloc");
                        *stpncpy(stpcpy(stpncpy(allocatedName, filename,
                                nameWithoutExtLength), "."), newExt,
                                newExtLength) = '\0';
                    } else {
                        allocatedName = strndup(filename, nameWithoutExtLength);
                        if (!allocatedName) err(1, "strdup");
                    }

                    *outputName = allocatedName;
                    return algorithms[i];
                }
                extensions += strcspn(extensions, ",");
                if (*extensions) extensions++;
            }
        }
    }

    allocatedName = malloc(strlen(filename) + 3);
    if (!allocatedName) err(1, "malloc");
    stpcpy(stpcpy(allocatedName, filename), ".Z");
    *inputName = allocatedName;
    *outputName = filename;
    return algorithms[0];
}

static const struct algorithm* probe(int input, unsigned char* buffer,
        size_t* bufferUsed, size_t bufferSize) {
    while (bufferSize > 0) {
        ssize_t bytesRead = read(input, buffer + *bufferUsed, bufferSize);
        if (bytesRead < 0) {
            *bufferUsed = -1;
            return NULL;
        }
        if (bytesRead == 0) break;
        *bufferUsed += bytesRead;
        bufferSize -= bytesRead;
    }
    for (size_t i = 0; algorithms[i]; i++) {
        if (algorithms[i]->probe(buffer, *bufferUsed)) {
            return algorithms[i];
        }
    }
    return NULL;
}

static int processOperand(const char* filename) {
    const char* inputName = filename;
    const char* outputName = NULL;
    allocatedName = NULL;
    int input;
    int output;
    int status = 0;
    struct stat inputStat;
    if (strcmp(filename, "-") == 0) {
        input = 0;
        output = 1;
        inputName = "stdin";
        fstat(input, &inputStat);
    } else {
        if (!decompress) {
            if (!writeToStdout) {
                size_t extensionLength = strcspn(algorithm->extensions, ",");
                allocatedName = malloc(strlen(filename) + extensionLength + 2);
                if (!allocatedName) err(1, "malloc");
                *stpncpy(stpcpy(stpcpy(allocatedName, filename), "."),
                        algorithm->extensions, extensionLength) = '\0';
                outputName = allocatedName;
            }
        } else {
            size_t length = strlen(filename);
            bool fileExists = access(filename, F_OK) == 0;
            if (writeToStdout && fileExists) {
                // Use the filename as is.
            } else if (!fileExists && (length <= 2 ||
                    filename[length - 2] != '.' ||
                    filename[length - 1] != 'Z')) {
                allocatedName = malloc(length + 3);
                if (!allocatedName) err(1, "malloc");
                stpcpy(stpcpy(allocatedName, filename), ".Z");
                inputName = allocatedName;
                outputName = filename;
                algorithm = algorithms[0];
            } else {
                algorithm = handleExtensions(filename, &inputName, &outputName);
            }
        }

        input = open(inputName, O_RDONLY | O_NOFOLLOW);
        if (input < 0) {
            warn("cannot open '%s'", inputName);
            free(allocatedName);
            return 1;
        }
        fstat(input, &inputStat);
        if (!S_ISREG(inputStat.st_mode)) {
            warnx("cannot open '%s': Not a regular file", inputName);
            close(input);
            free(allocatedName);
            return 1;
        }

        if (writeToStdout) {
            output = 1;
        } else {
            if (force) {
                unlink(outputName);
            }
            output = open(outputName, O_WRONLY | O_CREAT | O_NOFOLLOW | O_EXCL,
                    0666);
            if (output < 0 && errno == EEXIST && !force) {
                if (getConfirmation(outputName)) {
                    unlink(outputName);
                    output = open(outputName,
                            O_WRONLY | O_CREAT | O_NOFOLLOW | O_EXCL, 0666);
                } else {
                    errno = EEXIST;
                }
            }
            if (output < 0) {
                warn("cannot create file '%s'", outputName);
                close(input);
                free(allocatedName);
                return 1;
            }
        }
    }

    if (verbose) {
        fprintf(stderr, "%s: ", inputName);
    }

    unsigned char buffer[6];
    size_t bufferUsed = 0;
    int result = RESULT_OK;
    if (output == 1 && decompress) {
        algorithm = probe(input, buffer, &bufferUsed, sizeof(buffer));
        if (!algorithm) {
            result = bufferUsed == (size_t) -1 ? RESULT_READ_ERROR :
                    RESULT_UNRECOGNIZED_FORMAT;
        }
    }

    double ratio;
    if (algorithm) {
        if (decompress) {
            result = algorithm->decompress(input, output, &ratio, buffer,
                    bufferUsed);
        } else {
            result = algorithm->compress(input, output, bits, &ratio);
        }
    }

    if (result != RESULT_OK) {
        warnx("failed to %scompress '%s': %s", decompress ? "de" : "",
                inputName,
                result == RESULT_FORMAT_ERROR ? "file format error" :
                result == RESULT_READ_ERROR ? "read error" :
                result == RESULT_WRITE_ERROR ? "write error" :
                result == RESULT_UNRECOGNIZED_FORMAT ? "unrecognized format" :
                "unknown error");
        status = 1;
    } else {
#if HAVE_FCHOWN
        if (fchown(output, inputStat.st_uid, inputStat.st_gid) < 0) {
            warn("cannot set ownership for '%s'", outputName);
        }
#endif
        fchmod(output, inputStat.st_mode);
        struct timespec ts[2] = { inputStat.st_atim, inputStat.st_mtim };
        futimens(output, ts);
    }

    if (input != 0) {
        close(input);
    }
    if (output != 1) {
        close(output);
    }

    if (output != 1 && status == 1) {
        unlink(outputName);
    } else if (output != 1 && ratio < 0.0 && !force && !decompress) {
        unlink(outputName);
        if (verbose) {
            fprintf(stderr, "No compression - file unchanged\n");
        }
        status = 2;
    } else if (status == 0) {
        if (output != 1 && unlink(inputName) < 0 && errno == EPERM) {
            warn("cannot unlink '%s'", inputName);
            if (!force) {
                unlink(outputName);
                status = 1;
            }
        } else if (verbose) {
            if (decompress) {
                fprintf(stderr, "Expansion %.2f%%", ratio * 100.0);
            } else {
                fprintf(stderr, "Compression %.2f%%", ratio * 100.0);
            }
            if (output != 1) {
                fprintf(stderr, " - replaced with '%s'", outputName);
            }
            fputc('\n', stderr);
        }
    }
    free(allocatedName);
    return status;
}
