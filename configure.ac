# -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
# vim:expandtab:shiftwidth=4:tabstop=4:
# Process this file with autoconf to produce a configure script.

AC_PREREQ(2.59)

AC_INIT([phobos], [1.0.1], [st-tgcc@cea.fr])
RELEASE="1"

AC_DEFINE_UNQUOTED(RELEASE, "$RELEASE", [release info])
AC_SUBST(RELEASE)

AC_CONFIG_AUX_DIR([autotools])
AC_CONFIG_MACRO_DIR([autotools/m4])
AC_CONFIG_HEADER([src/include/config.h])
AC_CONFIG_SRCDIR([src/include/pho_lrs.h])

AM_INIT_AUTOMAKE([-Wall -Werror tar-pax foreign])
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])

AC_PREFIX_DEFAULT([/usr])

AC_GNU_SOURCE

# only generate dynamic libs (used in LD_PRELOAD)
AC_ENABLE_STATIC(no)
# disable shared libraries generation
#AC_ENABLE_SHARED(no)

# required for automake 1.12 (since fedora 18)
m4_ifdef([AM_PROG_AR], [AM_PROG_AR])

AC_PROG_LIBTOOL
AC_PROG_CC
LT_INIT

AC_PROG_LN_S

# required for automake 1.12 (since fedora 18)
m4_ifdef([AM_PROG_CC_C_O], [AM_PROG_CC_C_O])

AC_C_INLINE

# define everything necessary for accessing large files (64bits offset)
AC_SYS_LARGEFILE

# gcc options
CC_OPT="-Wall -Werror -Wstrict-prototypes -Wmissing-prototypes -Wmissing-declarations -std=gnu99 -Wno-variadic-macros"
AC_SUBST(CC_OPT)

AC_CHECK_SIZEOF([size_t])

PKG_CHECK_MODULES(GLIB2, [glib-2.0])
PKG_CHECK_MODULES(GTHREAD2, [gthread-2.0])
CFLAGS="$CFLAGS $GLIB2_CFLAGS $GTHREAD2_CFLAGS"
LDFLAGS="$LDFLAGS $GLIB2_LIBS $GTHREAD2_LIBS"

AC_CHECK_FUNC([g_list_free_full], AC_DEFINE(HAVE_GLIB_FREE_FULL, 1, [g_list_free_full is available since glib 2.28]))

CFLAGS="$CFLAGS -I\$(top_srcdir)/src/include"


AC_ARG_ENABLE( [cli], AS_HELP_STRING([--disable-cli],
               [Do not generate python cli and modules]),
               [enable_cli="$enableval"],[enable_cli="yes"] )
AS_IF( [test "x$enable_cli" != "xno"], [
    # Enable python bindings
    AX_PKG_SWIG(1.3.0, [], [ AC_MSG_ERROR([SWIG is required to build the CLI]) ])
    AX_PYTHON_DEVEL([>='2.6'])
])

# Checks for header files.
AC_HEADER_STDC
AC_CHECK_HEADERS([string.h sys/param.h limits.h])
AC_CHECK_HEADERS([jansson.h], [], [AC_MSG_ERROR(missing jansson header files)])
AC_CHECK_HEADERS([ini_config.h], [], [AC_MSG_ERROR(missing libini_config header files)])
AC_CHECK_HEADERS([openssl/sha.h], [], [AC_MSG_ERROR(missing openssl libcrypt header files)])
AC_CHECK_HEADERS([attr/xattr.h], [], [AC_MSG_ERROR(missing libattr header files)])
AC_CHECK_HEADERS([scsi/sg_io_linux.h], [], [AC_MSG_ERROR(missing sg3_utils header files)])
PKG_CHECK_MODULES(LIBPQ, [libpq >= 9.4])

# Checks for typedefs, structures, and compiler characteristics.
AC_C_CONST
AC_TYPE_UID_T
AC_TYPE_SIZE_T

# for exporting to spec file
AC_SUBST(ac_configure_args)

AC_CONFIG_FILES([Makefile
         src/Makefile
         src/include/Makefile
         src/common/Makefile
         src/ldm/Makefile
         src/dss/Makefile
         src/cfg/Makefile
         src/lrs/Makefile
         src/io/Makefile
         src/layout/Makefile
         src/store/Makefile
         src/layout-modules/Makefile
         src/tests/Makefile
         src/cli/Makefile
         phobos.spec
])
AC_OUTPUT
