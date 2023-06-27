AM_CFLAGS= $(CC_OPT)

sbin_PROGRAMS=tlc

tlc_SOURCES=tlc.c tlc_cfg.h tlc_cfg.c
tlc_CFLAGS=$(AM_CFLAGS)
tlc_LDADD=../cfg/libpho_cfg.la \
          ../common/libpho_common.la \
          ../communication/libpho_comm.la \
          ../daemon/libpho_daemon.la \
          ../serializer/libpho_serializer_tlc.la
tlc_LDFLAGS=-Wl,-rpath=$(libdir) -Wl,-rpath=$(pkglibdir)
