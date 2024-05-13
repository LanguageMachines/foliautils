#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.69])
AC_INIT([foliautils],[0.22],[lamasoftware@science.ru.nl]) #adapt version number in codemeta.json as well
AM_INIT_AUTOMAKE
AC_CONFIG_SRCDIR([.])
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_HEADERS([config.h])

# Checks for programs.
AC_PROG_CXX( [g++ c++] )

# when running tests, use CXX
AC_LANG([C++])

# check OpenMP support
AC_OPENMP
if test "x$ac_cv_prog_cxx_openmp" != "x"; then
  if test "x$ac_cv_prog_cxx_openmp" != "xunsupported"; then
    CXXFLAGS="$CXXFLAGS $OPENMP_CXXFLAGS"
    AC_DEFINE([HAVE_OPENMP], [1] , [Define to 1 if you have OpenMP] )
  else
    AC_MSG_NOTICE([We don't have OpenMP. Multithreaded operation is disabled])
  fi
fi

# use libtool
LT_INIT

#checks for libraries.

# Checks for header files.
AC_CHECK_HEADERS([fcntl.h mach/mach.h sys/time.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_TYPE_SIZE_T
AC_TYPE_INT8_T
AC_HEADER_STDBOOL
AC_C_INLINE
# Checks for library functions.
AC_CHECK_FUNCS([select])

if test $prefix = "NONE"; then
   prefix="$ac_default_prefix"
fi

PKG_PROG_PKG_CONFIG

if test "x$PKG_CONFIG_PATH" = x; then
    export PKG_CONFIG_PATH="$prefix/lib/pkgconfig"
else
    export PKG_CONFIG_PATH="$prefix/lib/pkgconfig:$PKG_CONFIG_PATH"
fi

AC_OSX_PKG( [icu4c] )

PKG_CHECK_MODULES( [TEXTCAT],
  [libexttextcat],
  [TEXTCAT_FOUND=1
   CXXFLAGS="$CXXFLAGS $TEXTCAT_CFLAGS"
   LIBS="$TEXTCAT_LIBS $LIBS"],
  [PKG_CHECK_MODULES( [TEXTCAT],
   [libtextcat],
   [TEXTCAT_FOUND=1
    CXXFLAGS="$CXXFLAGS $TEXTCAT_CFLAGS"
    LIBS="$TEXTCAT_LIBS $LIBS"],
   [TEXTCAT_FOUND=0]
   )]
 )

if test $TEXTCAT_FOUND = 1; then
   AC_DEFINE([HAVE_TEXTCAT], [1], [textcat])
else
# So, no pkg-config for textcat found.
#     Hopefully an old style version can be found???
  AC_CHECK_OLD_TEXTCAT
fi

if test $TEXTCAT_FOUND = 1; then
  AC_SEARCH_LM
fi

PKG_CHECK_MODULES([ICU], [icu-uc >= 50 icu-io] )
CXXFLAGS="$CXXFLAGS $ICU_CFLAGS"
LIBS="$ICU_LIBS $LIBS"

PKG_CHECK_MODULES([XML2], [libxml-2.0 >= 2.6.16] )
CXXFLAGS="$CXXFLAGS $XML2_CFLAGS"
LIBS="$XML2_LIBS $LIBS"

PKG_CHECK_MODULES([ticcutils], [ticcutils >= 0.34] )
CXXFLAGS="$ticcutils_CFLAGS $CXXFLAGS"
LIBS="$ticcutils_LIBS $LIBS"

PKG_CHECK_MODULES([ucto], [ucto >= 0.9.4] )
CXXFLAGS="$ucto_CFLAGS $CXXFLAGS"
LIBS="$ucto_LIBS $LIBS"

PKG_CHECK_MODULES([folia], [folia >= 2.19] )
CXXFLAGS="$folia_CFLAGS $CXXFLAGS"
LIBS="$folia_LIBS $LIBS"

AC_CONFIG_FILES([
  Makefile
  m4/Makefile
  src/Makefile
  include/Makefile
  include/foliautils/Makefile
  docs/Makefile
  tests/Makefile
])
AC_OUTPUT
