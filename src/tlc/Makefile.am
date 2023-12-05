AM_CFLAGS= $(CC_OPT)

sbin_PROGRAMS=tlc

unitdir=`pkg-config systemd --variable=systemdsystemunitdir`
unit_files=tlc.service
unit_DATA=$(unit_files)
EXTRA_DIST=$(unit_files)

tlc_SOURCES=tlc.c tlc_cfg.h tlc_cfg.c tlc_library.h tlc_library.c \
            ../ldm-modules/scsi_api.h
tlc_CFLAGS=$(AM_CFLAGS)
tlc_LDADD=../dss/libpho_dss.la \
          ../cfg/libpho_cfg.la \
          ../common/libpho_common.la \
          ../communication/libpho_comm.la \
          ../daemon/libpho_daemon.la \
          ../serializer/libpho_serializer_tlc.la \
          ../ldm-modules/libpho_scsi.la
tlc_LDFLAGS=-Wl,-rpath=$(libdir) -Wl,-rpath=$(pkglibdir)
