AM_CFLAGS= $(CC_OPT)
AM_LDFLAGS=-Wl,-rpath=$(libdir) -Wl,-rpath=$(pkglibdir)

lib_LTLIBRARIES=libphobos_admin.la

libphobos_admin_la_SOURCES=admin.c
libphobos_admin_la_LIBADD=../dss/libpho_dss.la \
			  ../common/libpho_common.la \
			  ../communication/libpho_comm.la \
			  ../serializer/libpho_serializer.la
