AM_CFLAGS= $(CC_OPT)

SUBDIRS=scsi

sbin_PROGRAMS=tlc

noinst_LTLIBRARIES=libpho_tlc.la

unitdir=`pkg-config systemd --variable=systemdsystemunitdir`
unit_files=tlc.service
unit_DATA=$(unit_files)
EXTRA_DIST=$(unit_files)

tlc_SOURCES=tlc.c tlc_cfg.h tlc_cfg.c tlc_library.h tlc_library.c \
            scsi/scsi_api.h
tlc_CFLAGS=$(AM_CFLAGS) -Iscsi
tlc_LDADD=../dss/libpho_dss.la \
          ../cfg/libpho_cfg.la \
          ../common/libpho_common.la \
          ../communication/libpho_comm.la \
          ../daemon/libpho_daemon.la \
          ../serializer/libpho_serializer_tlc.la \
          scsi/libpho_scsi.la
tlc_LDFLAGS=-Wl,-rpath=$(libdir) -Wl,-rpath=$(pkglibdir)

libpho_tlc_la_SOURCES=tlc_cfg.h tlc_cfg.c tlc_library.h tlc_library.c
libpho_tlc_la_CFLAGS=$(AM_CFLAGS) -Iscsi
