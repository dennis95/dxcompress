# DX_CHECK_CFLAGS(FLAG)
# Check whether the C compiler accepts FLAG and append it to CFLAGS if accepted.
AC_DEFUN([DX_CHECK_CFLAGS],
[AS_VAR_PUSHDEF([dx_cache_var], [AS_TR_SH([dx_cv_cflags_$1])])dnl
AC_CACHE_CHECK([whether $CC accepts $1], [dx_cache_var],
    [dx_save_CFLAGS="$CFLAGS"
    CFLAGS="$CFLAGS $1"
    AC_COMPILE_IFELSE([AC_LANG_PROGRAM()], [dx_cache_var=yes],
        [dx_cache_var=no])
    CFLAGS="$dx_save_CFLAGS"])
AS_VAR_IF([dx_cache_var], [yes], [CFLAGS="$CFLAGS $1"])
AS_VAR_POPDEF([dx_cache_var])dnl
])
