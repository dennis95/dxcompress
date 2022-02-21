# DX_ENABLE_WRAPPERS
# Decide which wrappers scripts should be installed.
AC_DEFUN([DX_ENABLE_WRAPPERS],
[dx_wrap_gzip=no
dx_wrap_xz=no
AC_ARG_ENABLE([wrappers], [AS_HELP_STRING([--enable-wrappers=WRAPPERS],
    [create wrapper scripts for the given utilities])],
    [dx_save_IFS=$IFS
    IFS=,
    for dx_value in $enableval; do
        case $dx_value in
        gzip) dx_wrap_gzip=yes ;;
        xz)   dx_wrap_xz=yes ;;
        no) ;;
        *) AC_MSG_WARN([unknown wrapper name $dx_value])
        esac
    done
    IFS=$dx_save_IFS[]])
WRAPPERS="uncompress zcat"
AS_VAR_IF([dx_wrap_gzip], [yes], [AS_VAR_IF([dx_cv_pkg_config_zlib],
    [yes], [WRAPPERS="$WRAPPERS gzip gunzip"],
    [AC_MSG_WARN([cannot provide gzip wrappers without zlib])])])
AS_VAR_IF([dx_wrap_xz], [yes], [AS_VAR_IF([dx_cv_pkg_config_liblzma],
    [yes], [WRAPPERS="$WRAPPERS xz unxz xzcat"],
    [AC_MSG_WARN([cannot provide xz wrappers without liblzma])])])
AC_SUBST([WRAPPERS])dnl
])
