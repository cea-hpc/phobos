AM_CFLAGS= $(CC_OPT)

noinst_LTLIBRARIES=libpho_scsi.la

noinst_HEADERS=scsi_common.h scsi_api.h

libpho_scsi_la_SOURCES=scsi_common.c scsi_api.c
libpho_scsi_la_LIBADD=-lsgutils2

pkglib_LTLIBRARIES=libpho_lib_adapter_dummy.la libpho_lib_adapter_scsi.la \
                   libpho_dev_adapter_dir.la

libpho_lib_adapter_dummy_la_SOURCES=ldm_lib_dummy.c
libpho_lib_adapter_dummy_la_CFLAGS=-fPIC $(AM_CFLAGS)
libpho_lib_adapter_dummy_la_LIBADD=../common/libpho_common.la
libpho_lib_adapter_dummy_la_LDFLAGS=-version-info 0:0:0

libpho_lib_adapter_scsi_la_SOURCES=ldm_lib_scsi.c
libpho_lib_adapter_scsi_la_CFLAGS=-fPIC $(AM_CFLAGS)
libpho_lib_adapter_scsi_la_LIBADD=../common/libpho_common.la \
                                  ../cfg/libpho_cfg.la libpho_scsi.la
libpho_lib_adapter_scsi_la_LDFLAGS=-version-info 0:0:0

libpho_dev_adapter_dir_la_SOURCES=ldm_dev_dir.c
libpho_dev_adapter_dir_la_CFLAGS=-fPIC $(AM_CFLAGS)
libpho_dev_adapter_dir_la_LIBADD=../common/libpho_common.la
libpho_dev_adapter_dir_la_LDFLAGS=-version-info 0:0:0
