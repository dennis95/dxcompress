# DX_PROG_PKG_CONFIG
# Check for the pkg-config program.
AC_DEFUN([DX_PROG_PKG_CONFIG],
[AC_ARG_VAR([PKG_CONFIG], [path to the pkg-config program])dnl
AC_CHECK_TOOL([PKG_CONFIG], [pkg-config])
AS_VAR_IF([PKG_CONFIG], [],
    [AC_MSG_ERROR([The pkg-config program was not found.])])dnl
])

# DX_PKG_CONFIG_LIB(LIBRARY, ACTION-IF-FOUND)
# Set CFLAGS and LIBS as needed for the given LIBRARY. Run ACTION-IF-FOUND if
# the library is found.
AC_DEFUN([DX_PKG_CONFIG_LIB],
[AC_REQUIRE([DX_PROG_PKG_CONFIG])dnl
AC_CACHE_CHECK([for $1 library], [dx_cv_pkg_config_$1],
    [AS_IF([$PKG_CONFIG --exists $1], [dx_cv_pkg_config_$1=yes],
    [dx_cv_pkg_config_$1=no])])
AS_VAR_IF([dx_cv_pkg_config_$1], [yes],
    [AC_CACHE_CHECK([for CFLAGS needed for $1], [dx_cv_pkg_config_$1_cflags],
        [dx_cv_pkg_config_$1_cflags=$($PKG_CONFIG --cflags $1)])
    AC_CACHE_CHECK([for LIBS needed for $1], [dx_cv_pkg_config_$1_libs],
        [dx_cv_pkg_config_$1_libs=$($PKG_CONFIG --libs $1)])
    CFLAGS="$CFLAGS $dx_cv_pkg_config_$1_cflags"
    LIBS="$dx_cv_pkg_config_$1_libs $LIBS"
    $2])dnl
])
