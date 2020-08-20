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
#include "lzw.h"

static bool getConfirmation(const char* filename);
static int processOperand(const char* filename);

static unsigned long bits = 16;
static bool decompress = false;
static bool force = false;
static bool verbose = false;
static bool writeToStdout = false;

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

    int c;
    while ((c = getopt_long(argc, argv, "b:cdfhvV", longopts, NULL)) != -1) {
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
"  -v, --verbose            print filenames and compression ratios\n"
"  -V, --version            display version info\n",
argv[0]);
            return 0;
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

static int processOperand(const char* filename) {
    const char* inputName = filename;
    const char* outputName = NULL;
    char* allocatedName = NULL;
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
                allocatedName = malloc(strlen(filename) + 3);
                if (!allocatedName) err(1, "malloc");
                stpcpy(stpcpy(allocatedName, filename), ".Z");
                outputName = allocatedName;
            }
        } else {
            size_t length = strlen(filename);

            if (writeToStdout && access(filename, F_OK) == 0) {
                // Use the filename as is.
            } else if (length <= 2 || filename[length - 2] != '.' ||
                    filename[length - 1] != 'Z') {
                allocatedName = malloc(length + 3);
                if (!allocatedName) err(1, "malloc");
                stpcpy(stpcpy(allocatedName, filename), ".Z");
                inputName = allocatedName;
                outputName = filename;
            } else if (!writeToStdout) {
                allocatedName = strndup(filename, length - 2);
                if (!allocatedName) err(1, "strdup");
                outputName = allocatedName;
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
            output = open(outputName, O_WRONLY | O_CREAT | O_NOFOLLOW |
                    (force ? O_TRUNC : O_EXCL), 0666);
            if (output < 0 && errno == EEXIST) {
                if (getConfirmation(outputName)) {
                    output = open(outputName,
                            O_WRONLY | O_CREAT | O_NOFOLLOW | O_TRUNC, 0666);
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
            struct stat outputStat;
            fstat(input, &outputStat);
            if (!S_ISREG(outputStat.st_mode)) {
                warnx("cannot create file '%s': File exists and is not regular",
                        inputName);
                close(input);
                close(output);
                free(allocatedName);
                return 1;
            }
        }
    }

    if (verbose) {
        fprintf(stderr, "%s: ", inputName);
    }
    double ratio;
    int result = decompress ? lzwDecompress(input, output, &ratio) :
            lzwCompress(input, output, bits, &ratio);
    if (result != RESULT_OK) {
        warn("failed to %scompress '%s': %s", decompress ? "de" : "", inputName,
                result == RESULT_FORMAT_ERROR ? "file format error" :
                result == RESULT_READ_ERROR ? "read error" :
                result == RESULT_WRITE_ERROR ? "write error" : "unknown error");
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
