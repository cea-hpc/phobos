AM_CFLAGS= $(CC_OPT)

SUBDIRS=scsi

sbin_PROGRAMS=phobos_tlc

noinst_LTLIBRARIES=libpho_tlc.la

unitdir=`pkg-config systemd --variable=systemdsystemunitdir`
unit_files=phobos_tlc.service
unit_DATA=$(unit_files)
EXTRA_DIST=$(unit_files)

phobos_tlc_SOURCES=tlc.c tlc_cfg.h tlc_cfg.c tlc_library.h tlc_library.c \
            scsi/scsi_api.h
phobos_tlc_CFLAGS=$(AM_CFLAGS) -Iscsi
phobos_tlc_LDADD=../core/libpho_core.la ../daemon/libpho_daemon.la \
          scsi/libpho_scsi.la
phobos_tlc_LDFLAGS=-Wl,-rpath=$(libdir) -Wl,-rpath=$(pkglibdir)

libpho_tlc_la_SOURCES=tlc_cfg.h tlc_cfg.c tlc_library.h tlc_library.c
libpho_tlc_la_CFLAGS=$(AM_CFLAGS) -Iscsi
