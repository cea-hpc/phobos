AM_CFLAGS= $(CC_OPT)

sbin_PROGRAMS=tlc

tlc_SOURCES=tlc.c tlc_cfg.h tlc_cfg.c
tlc_CFLAGS=$(AM_CFLAGS)
tlc_LDADD=../common/libpho_common.la ../communication/libpho_comm.la \
          ../daemon/libpho_daemon.la
tlc_LDFLAGS=-Wl,-rpath=$(libdir) -Wl,-rpath=$(pkglibdir)
