AM_CFLAGS= $(CC_OPT)

noinst_HEADERS=raid1.h

pkglib_LTLIBRARIES=libpho_layout_raid1.la
libpho_layout_raid1_la_SOURCES=raid1.c
libpho_layout_raid1_la_CFLAGS=-fPIC $(AM_CFLAGS)
libpho_layout_raid1_la_LIBADD=../store/libphobos_store.la
libpho_layout_raid1_la_LDFLAGS=-version-info 0:0:0
