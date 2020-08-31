#! /bin/sh
# Copyright (c) 2020 Dennis Wölfing
#
# Permission to use, copy, modify, and/or distribute this software for any
# purpose with or without fee is hereby granted, provided that the above
# copyright notice and this permission notice appear in all copies.
#
# THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
# WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
# MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
# ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
# WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
# ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
# OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

if test -z "$COMPRESS"; then
    COMPRESS=compress
    : ${UNCOMPRESS=uncompress}
else
    : ${UNCOMPRESS="$COMPRESS -d"}
fi

# Select which features to test.
: ${test_posix202x=yes}

compress() {
    command $COMPRESS "$@"
}

uncompress() {
    command $UNCOMPRESS "$@"
}

testdir="$PWD/tests"
rm -rf "$testdir"
mkdir -p "$testdir"
cd "$testdir"

success=true

compressibleFile() {
    cat << EOF
This file is very very repetitive repetitive repetetive. It should therefore
compress well. This file is very very repetitive repetitive repetetive. It
should therefore compress well. This file is very very repetitive repetitive
repetetive. It should therefore compress well. This file is very very repetitive
repetitive repetetive. It should therefore compress well. This file is very very
repetitive repetitive repetetive. It should therefore compress well. This file
is very very repetitive repetitive repetetive. It should therefore compress
well. This file is very very repetitive repetitive repetetive. It should
therefore compress well. This file is very very repetitive repetitive
repetetive. It should therefore compress well.
EOF
}

incompressibleFile() {
    # Create a file that probably shouldn't result in a size decrease when
    # compressed.
    cat << EOF
abcdefghijklmnopqrtuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZaAbBcCdDeEfFgGhHiIjJkKlLmMnNo
OpPqQrRsStTuUvVwWxXyYzZAaBbCcDdEeFfGgHhIiJjKkLlMmNnOoPpQqRrSsTtUuVvWwXxYyZz
EOF
}

fail() {
    echo "FAIL at line $1: $2"
    success=false
}

# Prevent compress from prompting input.
exec < /dev/null

###############
# Basic tests #
###############

# Check compression and decompression
compressibleFile > foo
compress foo || fail $LINENO "Compression failed"
test ! -e foo || fail $LINENO "Input file was not unlinked"
test -e foo.Z || fail $LINENO "Output file was not created"

uncompress foo.Z || fail $LINENO "Decompression failed"
test -e foo || fail $LINENO "Output file was not created"
test ! -e foo.Z || fail $LINENO "Input file was not unlinked"

# Check that contents match
compressibleFile > compare
cmp -s foo compare || fail $LINENO "Decompressed file contents are incorrect"
rm -f foo foo.Z

# Check decompression without .Z extension
compressibleFile > foo
compress foo
uncompress foo || fail $LINENO "Decompression failed"
test -e foo || fail $LINENO "Output file was not created"
test ! -e foo.Z || fail $LINENO "Input file was not unlinked"
cmp -s foo compare || fail $LINENO "Decompressed file contents are incorrect"
rm -f foo foo.Z

# Check whether various bits values work
for bits in 9 10 11 12 13 14
do
    compressibleFile > foo
    compress -b $bits foo || fail $LINENO "Compression with -b $bits failed"
    test ! -e foo || fail $LINENO "Input file was not unlinked"
    test -e foo.Z || fail $LINENO "Output file was not created"

    uncompress foo.Z || fail $LINENO "Decompression for -b $bits failed"
    test -e foo || fail $LINENO "Output file was not created"
    test ! -e foo.Z || fail $LINENO "Input file was not unlinked"

    cmp -s foo compare || fail $LINENO "Decompressed file contents for -b $bits are incorrect"
    rm -f foo foo.Z
done

rm -f compare

# Check behavior when the file cannot be compressed
incompressibleFile > foo
compress foo
test $? = 2 || fail $LINENO "Exit status incorrect"
test -e foo || fail $LINENO "Input file was unlinked"
test ! -e foo.Z || fail $LINENO "Output file was unexpectedly created"

incompressibleFile > compare
cmp -s foo compare || fail $LINENO "Input file was modified"
rm -f foo foo.Z

# Check that -f forces compression even if it makes the file larger
incompressibleFile > foo
compress -f foo || fail $LINENO "Compression failed"
test ! -e foo || fail $LINENO "Input file was not unlinked"
test -e foo.Z || fail $LINENO "Output file was not created"

uncompress foo.Z || fail $LINENO "Decompression failed"
test -e foo || fail $LINENO "Output file was not created"
test ! -e foo.Z || fail $LINENO "Input file was not unlinked"
cmp -s foo compare || fail $LINENO "Decompressed file contents are incorrect"
rm -f foo foo.Z compare

# Check that compress does not overwrite files without -f
compressibleFile > foo
touch foo.Z
msg="$(compress foo 2>&1 >/dev/null)"
status=$?
test $status != 0 && test $status != 2 || fail $LINENO "Exit status incorrect"
test -n "$msg" || fail $LINENO "Diagnostic message missing"
test -e foo || fail $LINENO "Input file was unlinked"
test -e foo.Z || fail $LINENO "Output file was unlinked"
compress -f foo || fail $LINENO "Compression failed"
test ! -e foo || fail $LINENO "Input file was not unlinked"
test -e foo.Z || fail $LINENO "Output file was not created"

touch foo
msg="$(uncompress foo.Z 2>&1 >/dev/null)"
status=$?
test $status != 0 && test $status != 2 || fail $LINENO "Exit status incorrect"
test -n "$msg" || fail $LINENO "Diagnostic message missing"
test -e foo || fail $LINENO "Output file was unlinked"
test -e foo.Z || fail $LINENO "Input file was unlinked"
uncompress -f foo.Z || fail $LINENO "Decompression failed"
test -e foo || fail $LINENO "Output file was not created"
test ! -e foo.Z || fail $LINENO "Input file was not unlinked"
compressibleFile > compare
cmp -s foo compare || fail $LINENO "Decompressed file contents are incorrect"
rm -f foo foo.Z

# Check that compress adds .Z extension even if the file without it exists
compressibleFile > foo
compress foo || fail $LINENO "Compression failed"
touch foo
uncompress -f foo || fail $LINENO "Decompression failed"
test -e foo || fail $LINENO "Output file was not created"
test ! -e foo.Z || fail $LINENO "Input file was not unlinked"
cmp -s foo compare || fail $LINENO "Decompressed file contents are incorrect"

# Check that compression from and to standard input/output works
compressibleFile | compress > foo.Z || fail $LINENO "Compression failed"
uncompress < foo.Z > foo || fail $LINENO "Decompression failed"
cmp -s foo compare || fail $LINENO "Decompressed file contents are incorrect"
rm -f foo foo.Z

compressibleFile > foo
compress -c foo > foo.Z || fail $LINENO "Compression failed"
test -e foo || fail $LINENO "Input file was unlinked"
uncompress -c foo.Z > foo || fail $LINENO "Decompression failed"
test -e foo.Z || fail $LINENO "Input file was unlinked"
cmp -s foo compare || fail $LINENO "Decompressed file contents are incorrect"
rm -f foo foo.Z compare

# Check that compressing from standard input always compresses
incompressibleFile | compress > foo.Z || fail $LINENO "Compression failed"
uncompress foo.Z || fail $LINENO "Decompression failed"
incompressibleFile > compare
cmp -s foo compare || fail $LINENO "Decompressed file contents are incorrect"
rm -f foo foo.Z compare

# Check that (de)compressing non-existent files fails.
msg="$(compress foo 2>&1 >/dev/null)" && fail $LINENO "Compression of non-existent file succeeded"
test -n "$msg" || fail $LINENO "Diagnostic message missing"
msg="$(uncompress foo 2>&1 >/dev/null)" && fail $LINENO "Decompression of non-existent file succeeded"
test -n "$msg" || fail $LINENO "Diagnostic message missing"

# Check that decompressing an empty file fails
touch foo.Z
msg="$(uncompress foo.Z 2>&1 >/dev/null)" && fail $LINENO "Decompression unexpectedly succeeded"
test -n "$msg" || fail $LINENO "Diagnostic message missing"
rm -f foo foo.Z

if test $test_posix202x = yes; then
#############################################
# Test features new in POSIX.1-202x draft 1 #
#############################################

# Check that compress -d works
compressibleFile > foo
compress foo || fail $LINENO "Compression failed"
compress -d foo.Z || fail $LINENO "Decompression failed"
test -e foo || fail $LINENO "Output file was not created"
test ! -e foo.Z || fail $LINENO "Input file was not unlinked"
rm -f foo foo.Z

# Check that -m lzw is accepted and that 15 and 16 bit compression works
compressibleFile > compare
for bits in 15 16
do
    compressibleFile > foo
    compress -m lzw -b $bits foo || fail $LINENO "Compression with -b $bits failed"
    test ! -e foo || fail $LINENO "Input file was not unlinked"
    test -e foo.Z || fail $LINENO "Output file was not created"

    uncompress foo.Z || fail $LINENO "Decompression for -b $bits failed"
    test -e foo || fail $LINENO "Output file was not created"
    test ! -e foo.Z || fail $LINENO "Input file was not unlinked"

    cmp -s foo compare || fail $LINENO "Decompressed file contents for -b $bits are incorrect"
    rm -f foo foo.Z
done

# Check gzip compression
compressibleFile > foo
compress -g foo || fail $LINENO "Compression with -g failed"
test ! -e foo || fail $LINENO "Input file was not unlinked"
test ! -e foo.Z || fail $LINENO "foo.Z was unexpectedly created"
test -e foo.gz || fail $LINENO "Output file was not created"

uncompress foo.gz || fail $LINENO "Decompression failed"
test -e foo || fail $LINENO "Output file was not created"
test ! -e foo.gz || fail $LINENO "Input file was not unlinked"
cmp -s foo compare || fail $LINENO "Decompressed file contents are incorrect"
rm -f foo foo.gz

# Check various compression levels
for level in 1 2 3 4 5 6 7 8 9
do
    compressibleFile > foo
    compress -m deflate -b $level foo || fail $LINENO "Compression with -b $level failed"
    test ! -e foo || fail $LINENO "Input file was not unlinked"
    test -e foo.gz || fail $LINENO "Output file was not created"

    uncompress foo.gz || fail $LINENO "Decompression for -b $level failed"
    test -e foo || fail $LINENO "Output file was not created"
    test ! -e foo.gz || fail $LINENO "Input file was not unlinked"

    cmp -s foo compare || fail $LINENO "Decompressed file contents for -b $level are incorrect"
    rm -f foo foo.gz
done

# Check that file extensions don't matter when decompressing with -c
compressibleFile > foo
compress -m gzip foo || fail $LINENO "Compression failed"
mv foo.gz foo.Z
compress -cd foo.Z > foo || fail $LINENO "Decompression failed"
cmp -s foo compare || fail $LINENO "Decompressed file contents are incorrect"
rm -f foo foo.Z compare

fi # test_posix202x

rm -rf "$testdir"

if $success; then
    echo "All tests passed."
else
    echo "Some tests failed."
    exit 1
fi