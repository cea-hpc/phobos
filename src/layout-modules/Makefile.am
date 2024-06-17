AM_CFLAGS= $(CC_OPT)

noinst_HEADERS=raid1.h raid4.h

pkglib_LTLIBRARIES=libpho_layout_raid1.la libpho_layout_raid4.la

libpho_layout_raid1_la_SOURCES=raid1.c
libpho_layout_raid1_la_CFLAGS=-fPIC $(AM_CFLAGS) -I../io-modules
libpho_layout_raid1_la_LIBADD=../store/libphobos_store.la \
                              ../io-modules/libpho_io_adapter_posix.la
libpho_layout_raid1_la_LDFLAGS=-version-info 0:0:0
if USE_XXHASH
libpho_layout_raid1_la_LDFLAGS+=-lxxhash
endif

libpho_layout_raid4_la_SOURCES=raid4.c
libpho_layout_raid4_la_CFLAGS=-fPIC $(AM_CFLAGS) -I ../layout
libpho_layout_raid4_la_LIBADD=../store/libphobos_store.la \
                              ../layout/libpho_layout_common.la
libpho_layout_raid4_la_LDFLAGS=-version-info 0:0:0
