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
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <inttypes.h>
#include <limits.h>
#include <stdarg.h>
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
    &algoDeflate,
    NULL
};

static const struct algorithm* getAlgorithm(const char* name);
static bool getConfirmation(const char* filename);
static const struct algorithm* handleExtensions(const char* filename,
        const char** inputName, const char** outputName, char** allocatedName);
static void list(const struct fileinfo* info);
static int nullDecompress(int input, int output, struct fileinfo* info,
        const unsigned char* buffer, size_t bufferSize);
static void outOfMemory(void);
static void printWarning(const char* format, ...);
static const struct algorithm* probe(int input, unsigned char* buffer,
        size_t* bufferUsed, size_t bufferSize);
static int processDirectory(int parentFd, const char* dirname,
        const char* pathname);
static int processFile(int dirFd, const char* inputName, const char* outputName,
        const char* inputPath, const char* outputPath);
static int processOperand(const char* filename);

static const struct algorithm algoNull = {
    .decompress = nullDecompress,
};

enum { MODE_COMPRESS, MODE_DECOMPRESS, MODE_TEST, MODE_LIST };
static const struct algorithm* algorithm;
static bool force = false;
static const char* givenOutputName = NULL;
static int level = -1;
static int mode = MODE_COMPRESS;
static const char* programName;
static bool recursive = false;
static bool verbose = false;
static bool writeToStdout = false;

int main(int argc, char* argv[]) {
    programName = argv[0];

    struct option longopts[] = {
        { "argv0", required_argument, 0, 0 },
        { "decompress", no_argument, 0, 'd' },
        { "force", no_argument, 0, 'f' },
        { "help", no_argument, 0, 'h' },
        { "list", no_argument, 0, 'l' },
        { "recursive", no_argument, 0, 'r' },
        { "stdout", no_argument, 0, 'c' },
        { "test", no_argument, 0, 't' },
        { "to-stdout", no_argument, 0, 'c' },
        { "uncompress", no_argument, 0, 'd' },
        { "verbose", no_argument, 0, 'v' },
        { "version", no_argument, 0, 'V' },
        { 0, 0, 0, 0 }
    };

    const char* algorithmName = NULL;

    int c;
    const char* opts = "123456789b:cdfghlm:o:OrtvV";
    while ((c = getopt_long(argc, argv, opts, longopts, NULL)) != -1) {
        switch (c) {
        case 0: // undocumented --argv0 option for internal use only
            programName = argv[0] = optarg;
            break;
        case '1': case '2': case '3': case '4': case '5': case '6': case '7':
        case '8': case '9':
            level = c - '0';
            if (!algorithmName) algorithmName = "gzip";
            break;
        case 'b': {
            char* end;
            unsigned long value = strtoul(optarg, &end, 10);
            if (*end || value > INT_MAX) {
                printWarning("invalid compression level: '%s'", optarg);
                return 1;
            }
            level = value;
        } break;
        case 'c':
            writeToStdout = true;
            break;
        case 'd':
            mode = MODE_DECOMPRESS;
            break;
        case 'f':
            force = true;
            break;
        case 'g':
            algorithmName = "gzip";
            break;
        case 'h':
            printf("Usage: %s [OPTIONS] [FILE...]\n"
"  -b LEVEL                 set the compression level\n"
"  -d, --decompress         decompress files\n"
"  -c, --stdout             write output to stdout\n"
"  -f, --force              force compression\n"
"  -g                       use the gzip algorithm for compression\n"
"  -h, --help               display this help\n"
"  -l, --list               list information about compressed files\n"
"  -m ALGO                  use the ALGO algorithm for compression\n"
"  -o FILENAME              write output to FILENAME\n"
"  -O                       use the lzw algorithm for compression\n"
"  -r, --recursive          recursively (de)compress files in directories\n"
"  -t, --test               check file integrity\n"
"  -v, --verbose            print filenames and compression ratios\n"
"  -V, --version            display version info\n",
argv[0]);
            return 0;
        case 'l':
            mode = MODE_LIST;
            break;
        case 'm':
            algorithmName = optarg;
            break;
        case 'o':
            givenOutputName = optarg;
            break;
        case 'O':
            algorithmName = "lzw";
            break;
        case 'r':
            recursive = true;
            break;
        case 't':
            mode = MODE_TEST;
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

    if (givenOutputName) {
        if (optind + 1 < argc) {
            printWarning("the -o option cannot be used with multiple input "
                    "files");
            return 1;
        }
        if (recursive || writeToStdout || mode == MODE_TEST ||
                mode == MODE_LIST) {
            printWarning("the -o option cannot be used with any of the -clrt "
                    "options");
            return 1;
        }
    }

    if (mode == MODE_COMPRESS) {
        if (!algorithmName) algorithmName = "lzw";
        algorithm = getAlgorithm(algorithmName);
        if (!algorithm) {
            printWarning("unknown compression algorithm '%s'", algorithmName);
            return 1;
        }
        if (level == -1) {
            level = algorithm->defaultLevel;
        } else if (!algorithm->checkLevel(level)) {
            printWarning("invalid compression level: '%d'", level);
            return 1;
        }
    } else if (mode == MODE_LIST) {
        if (verbose) {
            fputs("method  crc      date   time  ", stdout);
        }
        puts("compressed  uncompressed  ratio  uncompressed name");
    }

    int status = 0;
    if (optind >= argc) {
        status = processOperand("-");
    }

    for (int i = optind; i < argc; i++) {
        int result = processOperand(argv[i]);
        if (status == 0 || result == 1) status = result;
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
        const char** inputName, const char** outputName, char** allocatedName) {
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
                    if (inputName) *inputName = filename;
                    if (outputName) {
                        if (extensions[length] == ':') {
                            const char* newExt = extensions + extLength + 1;
                            size_t newExtLength = strcspn(newExt, ",");
                            *allocatedName = malloc(nameWithoutExtLength +
                                    newExtLength + 2);
                            if (!*allocatedName) outOfMemory();
                            *stpncpy(stpcpy(stpncpy(*allocatedName, filename,
                                    nameWithoutExtLength), "."), newExt,
                                    newExtLength) = '\0';
                        } else {
                            *allocatedName = strndup(filename,
                                    nameWithoutExtLength);
                            if (!*allocatedName) outOfMemory();
                        }

                        *outputName = *allocatedName;
                    }
                    return algorithms[i];
                }
                extensions += strcspn(extensions, ",");
                if (*extensions) extensions++;
            }
        }
    }

    if (inputName && outputName) {
        *allocatedName = malloc(strlen(filename) + 3);
        if (!*allocatedName) outOfMemory();
        stpcpy(stpcpy(*allocatedName, filename), ".Z");
        *inputName = *allocatedName;
        *outputName = filename;
        return algorithms[0];
    }
    return NULL;
}

static void list(const struct fileinfo* info) {
    if (verbose) {
        size_t nameLength = strcspn(algorithm->names, ",");
        printf("%-7.*s ", (int) nameLength, algorithm->names);
        printf("%.8"PRIx32" ", info->crc);
        struct tm* tm = localtime(&info->modificationTime.tv_sec);
        char buffer[13];
        if (!tm || !strftime(buffer, sizeof(buffer), "%b %d %H:%M", tm)) {
            buffer[0] = '\0';
        }
        printf("%12.12s ", buffer);
    }

    printf("%10jd  ", (intmax_t) info->compressedSize);
    printf("%12jd  ", (intmax_t) info->uncompressedSize);
    double ratio = 1.0 - (double) info->compressedSize /
            (double) info->uncompressedSize;
    printf("%4.1f%%  ", ratio * 100.0);
    printf("%s\n", info->name);
}

static int nullDecompress(int input, int output, struct fileinfo* info,
        const unsigned char* buffer, size_t bufferSize) {
    info->compressedSize = 1;
    info->uncompressedSize = 1;
    ssize_t writtenSize = writeAll(output, buffer, bufferSize);
    if (writtenSize < 0) return RESULT_WRITE_ERROR;

    while (true) {
        char readBuffer[8 * 4096];
        ssize_t readSize = read(input, readBuffer, sizeof(readBuffer));
        if (readSize == 0) return RESULT_OK;
        if (readSize < 0) return RESULT_READ_ERROR;

        writtenSize = writeAll(output, readBuffer, readSize);
        if (writtenSize < 0) return RESULT_WRITE_ERROR;
    }
}

static void outOfMemory(void) {
    printWarning("out of memory");
    exit(1);
}

static void printWarning(const char* format, ...) {
    va_list ap;
    va_start(ap, format);
    fputs(programName, stderr);
    fputs(": ", stderr);
    vfprintf(stderr, format, ap);
    fputc('\n', stderr);
    va_end(ap);
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

    if (mode == MODE_DECOMPRESS && force) {
        return &algoNull;
    }

    return NULL;
}

static int processDirectory(int parentFd, const char* dirname,
        const char* pathname) {
    int fd = openat(parentFd, dirname, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
    if (fd < 0) {
        printWarning("cannot open '%s': %s", pathname, strerror(errno));
        return 1;
    }
    DIR* dir = fdopendir(fd);
    if (!dir) {
        printWarning("cannot open '%s': %s", pathname, strerror(errno));
        return 1;
    }

    int status = 0;
    errno = 0;
    struct dirent* dirent = readdir(dir);
    while (dirent) {
        const char* name = dirent->d_name;
        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
            errno = 0;
            dirent = readdir(dir);
            continue;
        }

        size_t inputNameLength = strlen(name);
        char* inputPath = malloc(strlen(pathname) + inputNameLength + 2);
        if (!inputPath) outOfMemory();
        stpcpy(stpcpy(stpcpy(inputPath, pathname), "/"), name);

        struct stat st;
        if (fstatat(fd, name, &st, AT_SYMLINK_NOFOLLOW) == 0 &&
                S_ISDIR(st.st_mode)) {
            int result = processDirectory(fd, name, inputPath);
            if (status == 0 || result == 1) status = result;
        } else {
            const char* outputName;
            char* allocatedName = NULL;
            if (mode != MODE_COMPRESS) {
                algorithm = handleExtensions(name, NULL, &outputName,
                        &allocatedName);
            } else {
                // Skip files that already have the right extension so we don't
                // compress the same file multiple times.
                size_t extensionLength = strcspn(algorithm->extensions, ",");
                if (inputNameLength > extensionLength + 1 &&
                        name[inputNameLength - extensionLength - 1] == '.' &&
                        strncmp(&name[inputNameLength - extensionLength],
                        algorithm->extensions, extensionLength) == 0) {
                    free(inputPath);
                    errno = 0;
                    dirent = readdir(dir);
                    continue;
                }

                allocatedName = malloc(inputNameLength + extensionLength + 2);
                if (!allocatedName) outOfMemory();
                *stpncpy(stpcpy(stpcpy(allocatedName, name), "."),
                        algorithm->extensions, extensionLength) = '\0';
                outputName = allocatedName;
            }

            if (algorithm) {
                char* outputPath = malloc(strlen(pathname) + strlen(outputName)
                        + 2);
                if (!outputPath) outOfMemory();
                stpcpy(stpcpy(stpcpy(outputPath, pathname), "/"), outputName);
                int result = processFile(fd, name, outputName, inputPath,
                        outputPath);
                if (status == 0 || result == 1) status = result;
                free(outputPath);
            }
            free(allocatedName);
        }

        free(inputPath);
        errno = 0;
        dirent = readdir(dir);
    }

    if (errno) {
        printWarning("readdir: %s", strerror(errno));
        status = 1;
    }

    closedir(dir);
    return status;
}

static int processFile(int dirFd, const char* inputName, const char* outputName,
        const char* inputPath, const char* outputPath) {
    int input = 0;
    int output = 1;
    struct stat inputStat;
    if (inputName) {
        input = openat(dirFd, inputName, O_RDONLY | O_NOFOLLOW);
        if (input < 0) {
            printWarning("cannot open '%s': %s", inputPath, strerror(errno));
            return 1;
        }
        fstat(input, &inputStat);
        if (!S_ISREG(inputStat.st_mode)) {
            printWarning("cannot open '%s': Not a regular file", inputPath);
            close(input);
            return 1;
        }
    }

    if (outputName) {
        if (!writeToStdout && mode != MODE_TEST && mode != MODE_LIST) {
            if (force) {
                unlinkat(dirFd, outputName, 0);
            }
            output = openat(dirFd, outputName,
                    O_WRONLY | O_CREAT | O_NOFOLLOW | O_EXCL, 0666);
            if (output < 0 && errno == EEXIST && !force) {
                if (getConfirmation(outputPath)) {
                    unlinkat(dirFd, outputName, 0);
                    output = openat(dirFd, outputName,
                            O_WRONLY | O_CREAT | O_NOFOLLOW | O_EXCL, 0666);
                } else {
                    errno = EEXIST;
                }
            }
            if (output < 0) {
                printWarning("cannot create file '%s': %s", outputPath,
                        strerror(errno));
                close(input);
                return 1;
            }
        }
    }

    if (mode == MODE_TEST || mode == MODE_LIST) output = -1;

    if (verbose && mode != MODE_LIST) {
        fprintf(stderr, "%s: ", inputPath);
    }

    unsigned char buffer[6];
    size_t bufferUsed = 0;
    int result = RESULT_OK;
    if ((input == 0 || writeToStdout) && mode != MODE_COMPRESS) {
        algorithm = probe(input, buffer, &bufferUsed, sizeof(buffer));
        if (!algorithm) {
            result = bufferUsed == (size_t) -1 ? RESULT_READ_ERROR :
                    RESULT_UNRECOGNIZED_FORMAT;
        }
    }

    struct fileinfo info;
    if (input == 0) {
        info.name = NULL;
        clock_gettime(CLOCK_REALTIME, &info.modificationTime);
    } else {
        info.name = inputName;
        info.modificationTime = inputStat.st_mtim;
    }
    if (algorithm) {
        if (mode != MODE_COMPRESS) {
            result = algorithm->decompress(input, output, &info, buffer,
                    bufferUsed);
        } else {
            result = algorithm->compress(input, output, level, &info);
        }
    }

    int status = 0;
    if (result != RESULT_OK) {
        printWarning("failed to %s '%s': %s",
                mode == MODE_COMPRESS ? "compress" :
                mode == MODE_DECOMPRESS ? "decompress" :
                mode == MODE_TEST ? "verify" : "list",
                inputPath,
                result == RESULT_FORMAT_ERROR ? "file format error" :
                result == RESULT_READ_ERROR ? "read error" :
                result == RESULT_WRITE_ERROR ? "write error" :
                result == RESULT_UNRECOGNIZED_FORMAT ? "unrecognized format" :
                result == RESULT_OUT_OF_MEMORY ? "out of memory" :
                result == RESULT_UNIMPLEMENTED_FORMAT ?
                "file format unimplemented" : "unknown error");
        if (result == RESULT_OUT_OF_MEMORY) {
            // We shouldn't continue if we ran out of memory.
            if (output != 1 && output != -1) unlinkat(dirFd, outputName, 0);
            exit(1);
        }
        status = 1;
    } else if (input != 0 && output != 1 && output != -1) {
#if HAVE_FCHOWN
        if (fchown(output, inputStat.st_uid, inputStat.st_gid) < 0) {
            printWarning("cannot set ownership for '%s': %s", outputPath,
                    strerror(errno));
        }
#endif
        fchmod(output, inputStat.st_mode);
        struct timespec ts[2] = { inputStat.st_atim, inputStat.st_mtim };
        futimens(output, ts);
    }

    if (input != 0) {
        close(input);
    }
    if (output != 1 && output != -1) {
        close(output);
    }

    double ratio = info.uncompressedSize == 0 ? -1.0 :
            1.0 - (double) info.compressedSize / (double) info.uncompressedSize;

    if (output != 1 && output != -1 && status == 1) {
        unlinkat(dirFd, outputName, 0);
    } else if (output != 1 && ratio < 0.0 && !force && mode == MODE_COMPRESS) {
        unlinkat(dirFd, outputName, 0);
        if (verbose) {
            fprintf(stderr, "No compression - file unchanged\n");
        }
        status = 2;
    } else if (status == 0) {
        if (output != 1 && output != -1 && unlinkat(dirFd, inputName, 0) < 0 &&
                errno == EPERM) {
            printWarning("cannot unlink '%s': %s", inputPath, strerror(errno));
            if (!force) {
                unlinkat(dirFd, outputName, 0);
                status = 1;
            }
        } else if (mode == MODE_LIST) {
            info.modificationTime = inputStat.st_mtim;
            info.name = outputName ? outputName : outputPath;
            list(&info);
        } else if (verbose) {
            if (mode == MODE_DECOMPRESS) {
                fprintf(stderr, "Expansion %.2f%%", ratio * 100.0);
            } else if (mode == MODE_COMPRESS) {
                fprintf(stderr, "Compression %.2f%%", ratio * 100.0);
            } else if (mode == MODE_TEST) {
                fputs("OK", stderr);
            }
            if (output != 1 && output != -1) {
                fprintf(stderr, " - replaced with '%s'", outputPath);
            }
            fputc('\n', stderr);
        }
    }
    return status;
}

static int processOperand(const char* filename) {
    const char* inputName = filename;
    const char* outputName = NULL;
    char* allocatedName = NULL;
    bool isDirectory = false;
    if (strcmp(filename, "-") == 0) {
        inputName = NULL;
        if (givenOutputName) {
            outputName = givenOutputName;
        }
    } else {
        struct stat st;
        bool fileExists = true;
        if (lstat(filename, &st) < 0) {
            fileExists = false;
        } else if (recursive && S_ISDIR(st.st_mode)) {
            isDirectory = true;
        }

        if (!isDirectory && mode == MODE_COMPRESS) {
            if (givenOutputName) {
                outputName = givenOutputName;
            } else if (!writeToStdout) {
                size_t extensionLength = strcspn(algorithm->extensions, ",");
                allocatedName = malloc(strlen(filename) + extensionLength + 2);
                if (!allocatedName) outOfMemory();
                *stpncpy(stpcpy(stpcpy(allocatedName, filename), "."),
                        algorithm->extensions, extensionLength) = '\0';
                outputName = allocatedName;
            }
        } else if (!isDirectory) {
            size_t length = strlen(filename);
            if (writeToStdout && fileExists) {
                // Use the filename as is.
            } else if (givenOutputName) {
                outputName = givenOutputName;
                algorithm = handleExtensions(filename, NULL, NULL, NULL);
            } else if (!fileExists && (length <= 2 ||
                    filename[length - 2] != '.' ||
                    filename[length - 1] != 'Z')) {
                allocatedName = malloc(length + 3);
                if (!allocatedName) outOfMemory();
                stpcpy(stpcpy(allocatedName, filename), ".Z");
                inputName = allocatedName;
                outputName = filename;
                algorithm = algorithms[0];
            } else {
                algorithm = handleExtensions(filename, &inputName, &outputName,
                        &allocatedName);
            }
        }
    }

    int status;
    if (isDirectory) {
        status = processDirectory(AT_FDCWD, inputName, inputName);
    } else {
        status = processFile(AT_FDCWD, inputName, outputName,
                inputName ? inputName : "stdin",
                outputName ? outputName : "stdout");
    }

    free(allocatedName);
    return status;
}

ssize_t writeAll(int fd, const void* buffer, size_t size) {
    if (fd == -1) return size;
    size_t written = 0;
    while (written < size) {
        ssize_t result = write(fd, (char*) buffer + written, size - written);
        if (result < 0) return -1;
        written += result;
    }
    return written;
}
