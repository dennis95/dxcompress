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
    &algoXz,
    NULL
};

struct outputinfo {
    int dirFd;
    int outputFd;
    const char* dirPath;
    const char* outputName;
};

static const struct algorithm* getAlgorithm(const char* name);
static bool getConfirmation(const char* dirPath, const char* filename);
static bool hasSuffix(const char* string, const char* suffix);
static const struct algorithm* handleExtensions(const char* filename,
        const char** inputName, const char** outputName, char** allocatedName);
static void list(const struct fileinfo* info, const char* dirPath);
static int nullDecompress(int input, int output, struct fileinfo* info,
        const unsigned char* buffer, size_t bufferSize);
static void outOfMemory(void);
static void printWarning(const char* format, ...);
static const struct algorithm* probe(int input, unsigned char* buffer,
        size_t* bufferUsed, size_t bufferSize);
static int processDirectory(int parentFd, const char* dirname,
        const char* pathname);
static int processFile(int dirFd, const char* inputName, const char* outputName,
        const char* inputPath, const char* dirPath);
static int processOperand(const char* filename);

static const struct algorithm algoNull = {
    .decompress = nullDecompress,
};

enum { MODE_COMPRESS, MODE_DECOMPRESS, MODE_TEST, MODE_LIST };
static const struct algorithm* algorithm;
static bool force = false;
static const char* givenOutputName = NULL;
static bool keep = false;
static int level = -1;
static int mode = MODE_COMPRESS;
static bool restoreName = false;
static bool saveName = true;
static const char* programName;
static bool quiet = false;
static bool recursive = false;
static const char* suffix = NULL;
static bool verbose = false;
static bool writeToStdout = false;

int main(int argc, char* argv[]) {
    programName = argv[0];

    struct option longopts[] = {
        { "argv0", required_argument, 0, 1 },
        { "ascii", no_argument, 0, 'a' },
        { "best", no_argument, &level, -3 },
        { "decompress", no_argument, 0, 'd' },
        { "fast", no_argument, &level, -2 },
        { "force", no_argument, 0, 'f' },
        { "help", no_argument, 0, 'h' },
        { "keep", no_argument, 0, 'k' },
        { "list", no_argument, 0, 'l' },
        { "name", no_argument, 0, 'N' },
        { "no-name", no_argument, 0, 'n' },
        { "quiet", no_argument, 0, 'q' },
        { "recursive", no_argument, 0, 'r' },
        { "stdout", no_argument, 0, 'c' },
        { "suffix", required_argument, 0, 'S' },
        { "test", no_argument, 0, 't' },
        { "to-stdout", no_argument, 0, 'c' },
        { "uncompress", no_argument, 0, 'd' },
        { "verbose", no_argument, 0, 'v' },
        { "version", no_argument, 0, 'V' },
        { 0, 0, 0, 0 }
    };

    const char* algorithmName = NULL;

    int c;
    const char* opts = "0123456789ab:cdfghklm:nNo:OqrS:tvV";
    while ((c = getopt_long(argc, argv, opts, longopts, NULL)) != -1) {
        switch (c) {
        case 1: // undocumented --argv0 option for internal use only
            programName = argv[0] = optarg;
            break;
        case '0': case '1': case '2': case '3': case '4': case '5': case '6':
        case '7': case '8': case '9':
            level = c - '0';
            if (!algorithmName) algorithmName = "gzip";
            break;
        case 'a': // ignored for compatibility with gzip
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
"  -c, --stdout             write output to stdout\n"
"  -d, --decompress         decompress files\n"
"  -f, --force              force compression\n"
"  -g                       use the gzip algorithm for compression\n"
"  -h, --help               display this help\n"
"  -k, --keep               do not unlink input files\n"
"  -l, --list               list information about compressed files\n"
"  -m ALGO                  use the ALGO algorithm for compression\n"
"  -n, --no-name            do not save file name and time stamp\n"
"  -N, --name               use file name and time from compressed files\n"
"  -o FILENAME              write output to FILENAME\n"
"  -O                       use the lzw algorithm for compression\n"
"  -q, --quiet              suppress warning messages\n"
"  -r, --recursive          recursively (de)compress files in directories\n"
"  -S, --suffix=SUFFIX      use SUFFIX as suffix for compressed files\n"
"  -t, --test               check file integrity\n"
"  -v, --verbose            print filenames and compression ratios\n"
"  -V, --version            display version info\n",
argv[0]);
            return 0;
        case 'k':
            keep = true;
            break;
        case 'l':
            mode = MODE_LIST;
            break;
        case 'm':
            algorithmName = optarg;
            break;
        case 'n':
            restoreName = false;
            saveName = false;
            break;
        case 'N':
            restoreName = true;
            saveName = true;
            break;
        case 'o':
            givenOutputName = optarg;
            break;
        case 'O':
            algorithmName = "lzw";
            break;
        case 'q':
            quiet = true;
            verbose = false;
            break;
        case 'r':
            recursive = true;
            break;
        case 'S':
            if (optarg[0] == '.') optarg++;
            suffix = optarg;
            break;
        case 't':
            mode = MODE_TEST;
            break;
        case 'v':
            quiet = false;
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
        } else if (level == -2) {
            level = algorithm->minLevel;
        } else if (level == -3) {
            level = algorithm->maxLevel;
        } else if (level < algorithm->minLevel || level > algorithm->maxLevel) {
            printWarning("invalid compression level: '%d'", level);
            return 1;
        }
    } else if (mode == MODE_LIST && !quiet) {
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

static bool getConfirmation(const char* dirPath, const char* filename) {
    if (!isatty(0)) return false;
    fprintf(stderr, "File '%s%s%s' already exists, overwrite? ",
            dirPath ? dirPath : "", dirPath ? "/" : "", filename);
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

    if (suffix && hasSuffix(filename, suffix)) {
        if (inputName) *inputName = filename;
        if (outputName) {
            *allocatedName = strndup(filename,
                    strlen(filename) - strlen(suffix) - 1);
            if (!*allocatedName) outOfMemory();
            *outputName = *allocatedName;
        }
        return NULL;
    }

    if (inputName && outputName) {
        *allocatedName = malloc(strlen(filename) + 2 +
                (suffix ? strlen(suffix) : 1));
        if (!*allocatedName) outOfMemory();
        stpcpy(stpcpy(stpcpy(*allocatedName, filename), "."),
                suffix ? suffix : "Z");
        *inputName = *allocatedName;
        *outputName = filename;
        if (!suffix) return algorithms[0];
    }
    return NULL;
}

static bool hasSuffix(const char* string, const char* suffix) {
    size_t length = strlen(string);
    size_t suffixLength = strlen(suffix);
    if (length <= suffixLength + 1) return false;
    if (string[length - suffixLength - 1] != '.') return false;
    return strcmp(string + (length - suffixLength), suffix) == 0;
}

static void list(const struct fileinfo* info, const char* dirPath) {
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
    if (dirPath) {
        printf("%s/", dirPath);
    }
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

int openOutputFile(const char* outputName, struct outputinfo* oinfo) {
    if (!outputName) outputName = oinfo->outputName;
    if (force) {
        unlinkat(oinfo->dirFd, outputName, 0);
    }
    oinfo->outputFd = openat(oinfo->dirFd, outputName,
            O_WRONLY | O_CREAT | O_NOFOLLOW | O_EXCL, 0666);
    if (oinfo->outputFd < 0 && errno == EEXIST && !force) {
        if (getConfirmation(oinfo->dirPath, outputName)) {
            unlinkat(oinfo->dirFd, outputName, 0);
            oinfo->outputFd = openat(oinfo->dirFd, outputName,
                    O_WRONLY | O_CREAT | O_NOFOLLOW | O_EXCL, 0666);
        } else {
            errno = EEXIST;
        }
    }
    if (oinfo->outputFd < 0) {
        printWarning("cannot create file '%s%s%s': %s",
                oinfo->dirPath ? oinfo->dirPath : "", oinfo->dirPath ? "/" : "",
                outputName, strerror(errno));
    }
    return oinfo->outputFd;
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
                const char* extension = algorithm->extensions;
                size_t extensionLength = strcspn(extension, ",");
                if (suffix) {
                    extension = suffix;
                    extensionLength = strlen(suffix);
                }
                if (inputNameLength > extensionLength + 1 &&
                        name[inputNameLength - extensionLength - 1] == '.' &&
                        strncmp(&name[inputNameLength - extensionLength],
                        extension, extensionLength) == 0) {
                    free(inputPath);
                    errno = 0;
                    dirent = readdir(dir);
                    continue;
                }

                allocatedName = malloc(inputNameLength + extensionLength + 2);
                if (!allocatedName) outOfMemory();
                *stpncpy(stpcpy(stpcpy(allocatedName, name), "."), extension,
                        extensionLength) = '\0';
                outputName = allocatedName;
            }

            if (algorithm || suffix) {
                int result = processFile(fd, name, outputName, inputPath,
                        pathname);
                if (status == 0 || result == 1) status = result;
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
        const char* inputPath, const char* dirPath) {
    int input = 0;
    int output = 1;
    struct stat inputStat;

    struct outputinfo oinfo;
    oinfo.dirFd = dirFd;
    oinfo.outputFd = -1;
    oinfo.dirPath = dirPath;
    oinfo.outputName = outputName;

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
        if (!writeToStdout && mode == MODE_DECOMPRESS && restoreName) {
            output = -2;
        } else if (!writeToStdout && mode != MODE_TEST && mode != MODE_LIST) {
            output = openOutputFile(outputName, &oinfo);
            if (output < 0) {
                close(input);
                return 1;
            }
        }
    }

    if (mode == MODE_TEST || mode == MODE_LIST) output = -1;

    if (verbose && mode != MODE_LIST) {
        fprintf(stderr, "%s: ", inputPath ? inputPath : "stdin");
    }

    unsigned char buffer[6];
    size_t bufferUsed = 0;
    int result = RESULT_OK;
    if (mode != MODE_COMPRESS) {
        if (input == 0 || writeToStdout || (suffix && !algorithm)) {
            algorithm = probe(input, buffer, &bufferUsed, sizeof(buffer));
            if (!algorithm) {
                result = bufferUsed == (size_t) -1 ? RESULT_READ_ERROR :
                        RESULT_UNRECOGNIZED_FORMAT;
            }
        }
    }

    struct fileinfo info = {0};
    info.oinfo = &oinfo;
    if (mode == MODE_COMPRESS) {
        if (!saveName) {
            info.name = NULL;
            info.modificationTime.tv_sec = 0;
            info.modificationTime.tv_nsec = 0;
        } else if (input == 0) {
            info.name = NULL;
            clock_gettime(CLOCK_REALTIME, &info.modificationTime);
        } else {
            info.name = inputName;
            info.modificationTime = inputStat.st_mtim;
        }
    }
    if (algorithm) {
        if (mode != MODE_COMPRESS) {
            result = algorithm->decompress(input, output, &info, buffer,
                    bufferUsed);
        } else {
            result = algorithm->compress(input, output, level, &info);
        }
    }

    if (mode == MODE_DECOMPRESS && restoreName) {
        output = oinfo.outputFd;
        if (info.name) outputName = info.name;
    }

    int status = 0;
    if (result == RESULT_OPEN_FAILURE) {
        // An error message was already printed.
        status = 1;
    } else if (result != RESULT_OK) {
        printWarning("failed to %s '%s': %s",
                mode == MODE_COMPRESS ? "compress" :
                mode == MODE_DECOMPRESS ? "decompress" :
                mode == MODE_TEST ? "verify" : "list",
                inputPath ? inputPath : "stdin",
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
        if (fchown(output, inputStat.st_uid, inputStat.st_gid) < 0 && !quiet) {
            printWarning("cannot set ownership for '%s%s%s': %s",
                    dirPath ? dirPath : "", dirPath ? "/" : "", outputName,
                    strerror(errno));
        }
#endif
        fchmod(output, inputStat.st_mode);
        struct timespec ts[2] = { inputStat.st_atim, inputStat.st_mtim };
        if (restoreName && (info.modificationTime.tv_sec != 0 ||
                info.modificationTime.tv_nsec != 0)) {
            ts[0] = ts[1] = info.modificationTime;
        }
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
    } else if (input != 0 && output != 1 && ratio < 0.0 && !force &&
            mode == MODE_COMPRESS) {
        unlinkat(dirFd, outputName, 0);
        if (verbose) {
            fprintf(stderr, "No compression - file unchanged\n");
        }
        status = 2;
    } else if (status == 0) {
        if (input != 0 && output != 1 && output != -1 && !keep &&
                unlinkat(dirFd, inputName, 0) < 0 && errno == EPERM) {
            if (!quiet || !force) {
                printWarning("cannot unlink '%s': %s", inputPath,
                        strerror(errno));
            }
            if (!force) {
                unlinkat(dirFd, outputName, 0);
                status = 1;
            }
        } else if (mode == MODE_LIST) {
            bool nameReplaced = false;
            if (!restoreName) {
                info.modificationTime = inputStat.st_mtim;
                free((void*) info.name);
                info.name = outputName;
                nameReplaced = true;
            }
            if (info.modificationTime.tv_sec == 0 &&
                    info.modificationTime.tv_nsec == 0) {
                info.modificationTime = inputStat.st_mtim;
            }
            if (!info.name) {
                info.name = outputName ? outputName : "stdout";
                nameReplaced = true;
            }

            list(&info, dirPath);
            if (nameReplaced) info.name = NULL;
        } else if (verbose) {
            if (mode == MODE_DECOMPRESS) {
                fprintf(stderr, "Expansion %.2f%%", ratio * 100.0);
            } else if (mode == MODE_COMPRESS) {
                fprintf(stderr, "Compression %.2f%%", ratio * 100.0);
            } else if (mode == MODE_TEST) {
                fputs("OK", stderr);
            }
            if (output != 1 && output != -1) {
                fprintf(stderr, " - %s '%s%s%s'",
                        input != 0 && !keep ? "replaced with" : "created",
                        dirPath ? dirPath : "", dirPath ? "/" : "", outputName);
            }
            fputc('\n', stderr);
        }
    }
    if (mode != MODE_COMPRESS) {
        free((void*) info.name);
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
                const char* extension = algorithm->extensions;
                size_t extensionLength = strcspn(extension, ",");
                if (suffix) {
                    extension = suffix;
                    extensionLength = strlen(suffix);
                }
                allocatedName = malloc(strlen(filename) + extensionLength + 2);
                if (!allocatedName) outOfMemory();
                *stpncpy(stpcpy(stpcpy(allocatedName, filename), "."),
                        extension, extensionLength) = '\0';
                outputName = allocatedName;
            }
        } else if (!isDirectory) {
            if (writeToStdout && fileExists) {
                // Use the filename as is.
            } else if (givenOutputName) {
                outputName = givenOutputName;
                algorithm = handleExtensions(filename, NULL, NULL, NULL);
            } else if (!fileExists &&
                    !hasSuffix(filename, suffix ? suffix : "Z")) {
                allocatedName = malloc(strlen(filename) + 2 +
                        (suffix ? strlen(suffix) : 1));
                if (!allocatedName) outOfMemory();
                stpcpy(stpcpy(stpcpy(allocatedName, filename), "."),
                        suffix ? suffix : "Z");
                inputName = allocatedName;
                outputName = filename;
                algorithm =  suffix ? NULL : algorithms[0];
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
        status = processFile(AT_FDCWD, inputName, outputName, inputName, NULL);
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
