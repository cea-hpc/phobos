AM_CFLAGS= $(CC_OPT)

noinst_LTLIBRARIES=libpho_mapper.la

noinst_HEADERS=io_posix_common.h

libpho_mapper_la_SOURCES=mapper.c
libpho_mapper_la_LIBADD=-lcrypto

pkglib_LTLIBRARIES=libpho_io_adapter_posix.la libpho_io_adapter_ltfs.la

libpho_io_adapter_posix_la_SOURCES=io_posix.c io_posix_common.c
libpho_io_adapter_posix_la_CFLAGS=-fPIC $(AM_CFLAGS)
libpho_io_adapter_posix_la_LIBADD=../core/libpho_core.la libpho_mapper.la
libpho_io_adapter_posix_la_LDFLAGS=-version-info 0:0:0

libpho_io_adapter_ltfs_la_SOURCES=io_ltfs.c io_posix_common.c
libpho_io_adapter_ltfs_la_CFLAGS=-fPIC $(AM_CFLAGS)
libpho_io_adapter_ltfs_la_LIBADD=../core/libpho_core.la libpho_mapper.la
libpho_io_adapter_ltfs_la_LDFLAGS=-version-info 0:0:0

if RADOS_ENABLED
pkglib_LTLIBRARIES+=libpho_io_adapter_rados.la
libpho_io_adapter_rados_la_SOURCES=io_rados.c io_posix_common.c
libpho_io_adapter_rados_la_CFLAGS=-fPIC $(AM_CFLAGS)
libpho_io_adapter_rados_la_LIBADD=../core/libpho_core.la libpho_mapper.la \
        ../ldm/libpho_ldm.la -lrados
libpho_io_adapter_rados_la_LDFLAGS=-version-info 0:0:0
endif
