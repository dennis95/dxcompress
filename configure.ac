AC_PREREQ([2.69])
AC_INIT([dxcompress], [0.1])
AC_CONFIG_SRCDIR([lzw.c])
AC_CONFIG_HEADERS([config.h])

AC_PROG_CC
AC_USE_SYSTEM_EXTENSIONS
AC_SYS_LARGEFILE

# Dennix currently does not implement fchown.
AC_CHECK_FUNCS([fchown])

AC_CONFIG_FILES([Makefile])
AC_OUTPUT
