AM_CFLAGS= $(CC_OPT)
AM_LDFLAGS=-Wl,-rpath=$(libdir) -Wl,-rpath=$(pkglibdir)

# use full phobosd name here, as it is installed
# and can be used by client apps.
lib_LTLIBRARIES=libphobos_store.la

noinst_HEADERS=store_profile.h store_utils.h

libphobos_store_la_SOURCES=store.c store_list.c store_profile.c
libphobos_store_la_LIBADD=../cfg/libpho_cfg.la ../common/libpho_common.la \
			  ../communication/libpho_comm.la ../dss/libpho_dss.la \
			  ../module-loader/libpho_module_loader.la ../io/libpho_io.la \
			  ../layout/libpho_layout.la ../serializer/libpho_serializer.la
