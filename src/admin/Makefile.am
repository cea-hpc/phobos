AM_CFLAGS= $(CC_OPT)
AM_LDFLAGS=-Wl,-rpath=$(libdir) -Wl,-rpath=$(pkglibdir)

lib_LTLIBRARIES=libphobos_admin.la

libphobos_admin_la_SOURCES=admin.c admin_utils.h
libphobos_admin_la_LIBADD=../dss/libpho_dss.la ../cfg/libpho_cfg.la \
                          ../common/libpho_common.la \
                          ../communication/libpho_comm.la \
                          ../ldm/libpho_ldm.la \
                          ../import/libpho_import.la \
                          ../serializer/libpho_serializer.la \
                          ../serializer/libpho_serializer_tlc.la
