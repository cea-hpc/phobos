AM_CFLAGS= $(CC_OPT)
AM_LDFLAGS=-Wl,-rpath=$(libdir) -Wl,-rpath=$(pkglibdir)

lib_LTLIBRARIES=libphobos_admin.la

libphobos_admin_la_SOURCES=admin.c import.c import.h
libphobos_admin_la_LIBADD=../dss/libpho_dss.la ../cfg/libpho_cfg.la \
                          ../common/libpho_common.la \
                          ../communication/libpho_comm.la \
                          ../io/libpho_io.la ../ldm/libpho_ldm.la \
                          ../serializer/libpho_serializer.la \
                          ../serializer/libpho_serializer_tlc.la \
                          ../layout-modules/libpho_layout_raid1.la \
                          ../io-modules/libpho_io_adapter_posix.la \
                          ../io-modules/libpho_io_adapter_ltfs.la
libphobos_admin_la_CFLAGS=$(AM_CFLAGS) -I../io-modules -I../layout-modules
