AM_CFLAGS= $(CC_OPT)

# use full phobosd name here, as it is installed
# and can be used by client apps.
lib_LTLIBRARIES=libphobos_store.la

libphobos_store_la_SOURCES=store.c
libphobos_store_la_LIBADD=../common/libpho_common.la ../dss/libpho_dss.la \
			  ../cfg/libpho_cfg.la ../ldm/libpho_ldm.la \
			  ../lrs/libpho_lrs.la ../layout/libpho_layout.la \
			  ../io/libpho_io.la -lcrypto
