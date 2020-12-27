AC_PREREQ([2.69])
AC_INIT([webkitcacher], m4_esyscmd([git rev-list --count HEAD | tr -d '\n']), [tihmstar@gmail.com])

AC_CANONICAL_SYSTEM
AC_CANONICAL_HOST
AM_PROG_LIBTOOL

AM_INIT_AUTOMAKE([subdir-objects])
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_MACRO_DIRS([m4])

AC_DEFINE([VERSION_COMMIT_COUNT], "m4_esyscmd([git rev-list --count HEAD | tr -d '\n'])", [Git commit count])
AC_DEFINE([VERSION_COMMIT_SHA], "m4_esyscmd([git rev-parse HEAD | tr -d '\n'])", [Git commit sha])
AC_SUBST([VERSION_COMMIT_COUNT], ["m4_esyscmd([git rev-list --count HEAD | tr -d '\n'])"])
AC_SUBST([VERSION_COMMIT_SHA], ["m4_esyscmd([git rev-parse HEAD | tr -d '\n'])"])


# Checks for programs.
AC_PROG_CXX
AC_PROG_CC

LIBGENERAL_REQUIRES_STR="libgeneral >= 48"
SQLITE3_REQUIRES_STR="sqlite3 >= 1.3.1"

CFLAGS+=" -std=c11 -Wall -D_FILE_OFFSET_BITS=64"
CXXFLAGS+=" -std=c++14 -Wall -D_FILE_OFFSET_BITS=64"


# Checks for libraries.
PKG_CHECK_MODULES(libgeneral, $LIBGENERAL_REQUIRES_STR)
PKG_CHECK_MODULES(sqlite3, $SQLITE3_REQUIRES_STR)

AC_SUBST([libgeneral_requires], [$LIBGENERAL_REQUIRES_STR])
AC_SUBST([sqlite3_requires], [$SQLITE3_REQUIRES_STR])


AC_ARG_ENABLE([debug],
            [AS_HELP_STRING([--enable-debug],
            [enable debug build(default is no)])],
            [debug_build=true],
            [debug_build=false])


if test "$debug_build" = true; then
  echo "*** Note: debug build requested ***"
  CFLAGS+=" -g -DDEBUG=1 -O -g -fsanitize=address"
  CXXFLAGS+=" -g -DDEBUG=1 -O -g -fsanitize=address"
fi

# Checks for header files.
AC_CHECK_HEADERS([stdlib.h string.h stdio.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_CHECK_HEADER_STDBOOL
AC_TYPE_SIZE_T
AC_TYPE_UINT16_T
AC_TYPE_UINT32_T
AC_TYPE_UINT64_T

LT_INIT


AC_CONFIG_FILES([Makefile
                 webkitcacher/Makefile])
AC_OUTPUT

echo "
Configuration for $PACKAGE-$VERSION:
-------------------------------------------

  install prefix ..........: $prefix
  Debug build .............: $debug_build"

echo "  compiler ................: ${CC}

  Now type 'make' to build $PACKAGE-$VERSION,
  and then 'make install' for installation.
"
