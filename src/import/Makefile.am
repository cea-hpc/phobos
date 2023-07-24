AM_CFLAGS= $(CC_OPT)

noinst_LTLIBRARIES=libpho_import.la

libpho_import_la_SOURCES=media_import.c
libpho_import_la_LIBADD=../io-modules/libpho_io_adapter_posix.la
libpho_import_la_CFLAGS=$(AM_CFLAGS) -I../admin -I../io-modules \
						-I../layout-modules
