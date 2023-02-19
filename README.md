# dxcompress

dxcompress is a compression and decompression utility supporting multiple
compression algorithms. It currently supports the Lempel-Ziv-Welch (LZW)
algorithm, the DEFLATE (gzip) algorithm and the XZ compression format.

A single `compress` binary provides all the functionality. The scripts
`uncompress` and `zcat` are also provided as synonyms for `compress -d` and
`compress -cd`. Additional scripts `gzip`, `gunzip`, `xz`, `unxz`, and `xzcat`
can also be installed for compatibility with other compression utilities.

The `compress` utility should support all options that are recognized by most
compression utilities.

The `compress`, `uncompress`, and `zcat` utilities conform to the POSIX.1-2008
standard. All additional functionality required by the upcoming POSIX.1-202x
standard is also provided.

## Building

dxcompress uses an autoconf-generated configure script to configure the build.
When building from a git checkout, the configure script first needs to be
regenerated by running the `autogen.sh` script. The release tarballs already
contain the configure script.

The configure script accepts the usual options like `--prefix` to customize the
installation. Run `configure --help` for more information. Additionally the
`--enable-wrapper=WRAPPERS` option is also recogonized and causes additonal
wrapper scripts to be installed that behave like the compression and
decompression utilities for specific compression algorithms. `WRAPPERS` is a
comma-separated list of utilities. Valid utilities are `gzip` (installing `gzip`
and `gunzip`) and `xz` (installing `xz`, `unxz`, and `xzcat`).

After running the configure script, dxcompress can be built with `make` and
installed with `make install`. A testsuite can be run with `make check`.

## License

dxcompress is free software and is licensed under the terms of the ISC license.
The full license terms can be found in the `LICENSE` file.
