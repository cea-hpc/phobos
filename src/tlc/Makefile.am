AM_CFLAGS= $(CC_OPT)

sbin_PROGRAMS=tlc

tlc_SOURCES=tlc.c
tlc_CFLAGS=$(AM_CFLAGS)
tlc_LDADD=../common/libpho_common.la
tlc_LDFLAGS=-Wl,-rpath=$(libdir) -Wl,-rpath=$(pkglibdir)
