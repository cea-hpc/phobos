AM_CFLAGS= $(CC_OPT)

EXTRA_DIST= phobos_tape_library_mtx_test.bash

sbin_SCRIPTS=phobos_tape_library_mtx_test.bash
sbin_PROGRAMS=phobos_tape_library_test

phobos_tape_library_test_SOURCES=phobos_tape_library_test.c

phobos_tape_library_test_CFLAGS=$(AM_CFLAGS) -I../src/lrs/
phobos_tape_library_test_LDADD=../src/lrs/libpho_lrs.la \
        ../src/dss/libpho_dss.la \
        ../src/cfg/libpho_cfg.la \
        ../src/ldm/libpho_ldm.la \
        ../src/module-loader/libpho_module_loader.la \
        ../src/io/libpho_io.la \
        ../src/serializer/libpho_serializer.la \
        ../src/communication/libpho_comm.la \
        ../src/common/libpho_common.la

phobos_tape_library_test_LDFLAGS=-Wl,-rpath=$(libdir) \
                                 -Wl,-rpath=$(pkglibdir)
