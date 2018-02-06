#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ(2.61)
AC_INIT([foliautils],[0.8],[lamasoftware@science.ru.nl])
AM_INIT_AUTOMAKE
AC_CONFIG_SRCDIR([.])
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_HEADER([config.h])

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
AC_PROG_LIBTOOL
LT_INIT

#checks for libraries.

# Checks for header files.

# Checks for typedefs, structures, and compiler characteristics.
AC_TYPE_SIZE_T
AC_HEADER_STDBOOL

# Checks for library functions.

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

PKG_CHECK_MODULES([ICU], [icu-uc >= 50 icu-io] )
CXXFLAGS="$CXXFLAGS $ICU_CFLAGS"
LIBS="$ICU_LIBS $LIBS"

PKG_CHECK_MODULES([XML2], [libxml-2.0 >= 2.6.16] )
CXXFLAGS="$CXXFLAGS $XML2_CFLAGS"
LIBS="$XML2_LIBS $LIBS"

AC_CHECK_HEADERS([libexttextcat/textcat.h],
	         [CXXFLAGS="$CXXFLAGS -I$prefix/include"],
	         [AC_CHECK_HEADERS([libtextcat/textcat.h],
                                   [CXXFLAGS="$CXXFLAGS -I$prefix/include"],
		                   [AC_CHECK_HEADERS([textcat.h],
						     [],
		                                     [AC_MSG_ERROR([Desperatly seeking textcat header file, it is needed])])])])

AC_SEARCH_LIBS([textcat_Init],[exttextcat-2.0 exttextcat textcat],[],
  [AC_MSG_ERROR([Unable to find  textcat library. it is needed])])

PKG_CHECK_MODULES([ticcutils], [ticcutils >= 0.17] )
CXXFLAGS="$ticcutils_CFLAGS $CXXFLAGS"
LIBS="$ticcutils_LIBS $LIBS"

PKG_CHECK_MODULES([ucto], [ucto >= 0.9.4] )
CXXFLAGS="$ucto_CFLAGS $CXXFLAGS"
LIBS="$ucto_LIBS $LIBS"

PKG_CHECK_MODULES([folia], [folia >= 1.7] )
CXXFLAGS="$folia_CFLAGS $CXXFLAGS"
LIBS="$folia_LIBS $LIBS"

AC_CONFIG_FILES([
  Makefile
  m4/Makefile
  src/Makefile
  docs/Makefile
])
AC_OUTPUT
