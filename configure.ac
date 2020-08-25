m4_include([m4/check-cflags.m4])

AC_PREREQ([2.69])
AC_INIT([dxcompress], [0.1])
AC_CONFIG_SRCDIR([lzw.c])
AC_CONFIG_HEADERS([config.h])

AC_PROG_CC
AC_USE_SYSTEM_EXTENSIONS
AC_SYS_LARGEFILE

AC_ARG_WITH([zlib], [AS_HELP_STRING([--without-zlib],
 [disable gzip support through zlib])], [], [with_zlib=yes])
AS_IF([test "$with_zlib" != no],
[AC_CHECK_HEADER([zlib.h],
 [AC_SEARCH_LIBS([deflate], [z],
  [AC_DEFINE([WITH_ZLIB], [1], [Define to 1 if building with zlib.])])])])

# Dennix currently does not implement fchown.
AC_CHECK_FUNCS([fchown])

# Enable warnings on GCC.
DX_CHECK_CFLAGS([-Wall])
DX_CHECK_CFLAGS([-Wextra])
DX_CHECK_CFLAGS([-Werror=implicit-function-declaration])

AC_SUBST([cross_compiling])
AC_CONFIG_FILES([Makefile])
AC_OUTPUT
