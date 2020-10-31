m4_include([m4/check-cflags.m4])
m4_include([m4/pkg-config.m4])
m4_include([m4/wrappers.m4])

AC_PREREQ([2.69])
AC_INIT([dxcompress], [1.0])
AC_CONFIG_SRCDIR([lzw.c])
AC_CONFIG_HEADERS([config.h])
AC_ARG_PROGRAM

AC_PROG_CC
AC_USE_SYSTEM_EXTENSIONS
AC_SYS_LARGEFILE

AC_PROG_INSTALL
AC_CHECK_TOOL([STRIP], [strip], [:])

AC_ARG_WITH([liblzma], [AS_HELP_STRING([--without-liblzma],
 [disable xz support through liblzma])], [], [with_liblzma=yes])
AS_IF([test "$with_liblzma" != no],
[DX_PKG_CONFIG_LIB([liblzma],
 [AC_DEFINE([WITH_LIBLZMA], [1], [Define to 1 if building with liblzma.])])])

AC_ARG_WITH([zlib], [AS_HELP_STRING([--without-zlib],
 [disable gzip support through zlib])], [], [with_zlib=yes])
AS_IF([test "$with_zlib" != no],
[DX_PKG_CONFIG_LIB([zlib],
 [AC_DEFINE([WITH_ZLIB], [1], [Define to 1 if building with zlib.])])])

DX_ENABLE_WRAPPERS

# Dennix currently does not implement fchown.
AC_CHECK_FUNCS([fchown])

# Enable warnings on GCC.
DX_CHECK_CFLAGS([-Wall])
DX_CHECK_CFLAGS([-Wextra])
DX_CHECK_CFLAGS([-Werror=implicit-function-declaration])

AC_SUBST([cross_compiling])
AC_CONFIG_FILES([Makefile])
AC_OUTPUT
