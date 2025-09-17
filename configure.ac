# -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
# vim:expandtab:shiftwidth=4:tabstop=4:
# Process this file with autoconf to produce a configure script.

AC_PREREQ(2.59)

m4_define([PHOBOS_VERSION_MAJOR], [3])
m4_define([PHOBOS_VERSION_MINOR], [1])
m4_define([PHOBOS_VERSION_PATCH], [0])
m4_define([PHOBOS_VERSION_RELEASE], [0])

m4_define([PHOBOS_VERSION],
          [PHOBOS_VERSION_MAJOR.PHOBOS_VERSION_MINOR.PHOBOS_VERSION_PATCH])
AC_INIT([phobos], [PHOBOS_VERSION], [st-hpc@cea.fr])

AC_DEFINE_UNQUOTED(PHOBOS_MAJOR, PHOBOS_VERSION_MAJOR,
                   [Phobos major version number])
AC_DEFINE_UNQUOTED(PHOBOS_MINOR, PHOBOS_VERSION_MINOR,
                   [Phobos minor version number])
AC_DEFINE_UNQUOTED(PHOBOS_PATCH, PHOBOS_VERSION_PATCH,
                   [Phobos patch version number])
AC_DEFINE_UNQUOTED(PHOBOS_RELEASE, PHOBOS_VERSION_RELEASE, [release info])
AC_SUBST(PHOBOS_RELEASE)


AC_CONFIG_AUX_DIR([autotools])
AC_CONFIG_MACRO_DIR([autotools/m4])
AC_CONFIG_HEADER([src/include/config.h])

AM_INIT_AUTOMAKE([-Wall -Werror tar-pax foreign subdir-objects])
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])

AC_PREFIX_DEFAULT([/usr])

AC_GNU_SOURCE

AX_VALGRIND_DFLT([helgrind],[off])
AX_VALGRIND_DFLT([drd],[off])
AX_VALGRIND_DFLT([sgcheck],[off])
AX_VALGRIND_CHECK

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
CC_OPT="-Wall -Werror -Wstrict-prototypes -Wmissing-prototypes -Wmissing-declarations -std=gnu11 -Wno-variadic-macros"
AC_SUBST(CC_OPT)

AC_CHECK_SIZEOF([size_t])

PKG_CHECK_MODULES(GLIB2, [glib-2.0])
PKG_CHECK_MODULES(GTHREAD2, [gthread-2.0])
CFLAGS="$CFLAGS $GLIB2_CFLAGS $GTHREAD2_CFLAGS"
LDFLAGS="$LDFLAGS $GLIB2_LIBS $GTHREAD2_LIBS -lcrypto"

AC_CHECK_FUNC([g_list_free_full], AC_DEFINE(HAVE_GLIB_FREE_FULL, 1, [g_list_free_full is available since glib 2.28]))

CFLAGS="$CFLAGS -I\$(top_srcdir)/src/include"

AC_ARG_ENABLE( [cli], AS_HELP_STRING([--disable-cli],
               [Do not generate python cli and modules]),
               [enable_cli="$enableval"],[enable_cli="yes"] )

PYTHON=$(which python3)
AS_IF( [test "x$enable_cli" != "xno"], [
    # Enable python bindings
    AX_PYTHON_DEVEL([>='3.6'])
])
AM_PATH_PYTHON

AM_CONDITIONAL([NO_RPM_BUILD], [test "x$RPM_BUILD_ROOT" = "x"])

AC_ARG_ENABLE( [rados], AS_HELP_STRING([--enable-rados],
               [Compile with rados modules @<:@check@:>@]),
               [enable_rados="$enableval"], [enable_rados="no"])

AC_SUBST(HAVE_RADOS, [0])
# By default RADOS modules are not built
AS_IF([test "x$enable_rados" = "xyes"],
      [AC_CHECK_LIB([rados], [rados_create],
          [AC_SUBST(HAVE_RADOS, [1])
           AC_DEFINE([RADOS_ENABLED], ["1"], [Define if rados is enabled])],
          [AC_MSG_ERROR([Librados required, but not found.])
           enable_rados="no"])])

AM_CONDITIONAL([RADOS_ENABLED], [test "x$enable_rados" = "xyes"])

AC_CHECK_LIB([xxhash], [XXH3_128bits_reset],
             [AC_SUBST(HAVE_XXH128, 'yes')]
             [AC_DEFINE(HAVE_XXH128, 1,
                        [XXH128 is available since xxhash 0.8.0])],
             [AC_SUBST(HAVE_XXH128, 'no')])

AM_CONDITIONAL([USE_XXHASH],
               [test "x$ac_cv_lib_xxhash_XXH3_128bits_reset" = "xyes"])

# Checks for header files.
AC_HEADER_STDC
AC_CHECK_HEADERS([string.h sys/param.h limits.h])
AC_CHECK_HEADERS([jansson.h], [], [AC_MSG_ERROR(missing jansson header files)])
AC_CHECK_HEADERS([ini_config.h], [], [AC_MSG_ERROR(missing libini_config header files)])
AC_CHECK_HEADERS([openssl/sha.h], [], [AC_MSG_ERROR(missing openssl libcrypt header files)])
AC_CHECK_HEADERS([attr/xattr.h], [], [AC_MSG_ERROR(missing libattr header files)])
AC_CHECK_HEADERS([scsi/sg_io_linux.h], [], [AC_MSG_ERROR(missing sg3_utils header files)])
AC_CHECK_HEADERS([protobuf-c/protobuf-c.h], [], [AC_MSG_ERROR(missing protobuf-c header files)])
PKG_CHECK_MODULES(JANSSON, [jansson >= 2.5])
PKG_CHECK_MODULES(LIBPQ, [libpq >= 9.4])
PKG_CHECK_MODULES(CMOCKA, [cmocka >= 1.1], ,
    [AC_MSG_WARN([missing package libcmocka-devel, tests will fail to compile])]
)

# Checks for typedefs, structures, and compiler characteristics.
AC_C_CONST
AC_TYPE_UID_T
AC_TYPE_SIZE_T

# for exporting to spec file
AC_SUBST(ac_configure_args)

AC_CONFIG_FILES([Makefile
         src/Makefile
         src/include/Makefile
         src/module-loader/Makefile
         src/ldm/Makefile
         src/lrs/Makefile
         src/io/Makefile
         src/layout/Makefile
         src/core/Makefile
         src/core/serializer/Makefile
         src/store/Makefile
         src/io-modules/Makefile
         src/layout-modules/Makefile
         src/ldm-modules/Makefile
         src/admin/Makefile
         src/cli/Makefile
         src/tlc/Makefile
         src/tlc/scsi/Makefile
         src/daemon/Makefile
         src/hsm/Makefile
         tests/Makefile
         tests/interns/Makefile
         tests/externs/api/Makefile
         tests/externs/cli/Makefile
         tests/prod/Makefile
         doc/man/Makefile
         tape_library_certification/Makefile
         phobos.spec
])
AC_OUTPUT
