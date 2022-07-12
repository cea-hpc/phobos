AM_CFLAGS= $(CC_OPT)

noinst_LTLIBRARIES=libpho_scsi.la

noinst_HEADERS=ldm_common.h scsi_common.h scsi_api.h

libpho_scsi_la_SOURCES=scsi_common.c scsi_api.c
libpho_scsi_la_LIBADD=-lsgutils2

pkglib_LTLIBRARIES=libpho_lib_adapter_dummy.la libpho_lib_adapter_scsi.la \
                   libpho_dev_adapter_dir.la libpho_dev_adapter_scsi_tape.la \
                   libpho_fs_adapter_posix.la libpho_fs_adapter_ltfs.la

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

libpho_dev_adapter_scsi_tape_la_SOURCES=ldm_dev_scsi_tape.c
libpho_dev_adapter_scsi_tape_la_CFLAGS=-fPIC $(AM_CFLAGS)
libpho_dev_adapter_scsi_tape_la_LIBADD=../common/libpho_common.la
libpho_dev_adapter_scsi_tape_la_LDFLAGS=-version-info 0:0:0

libpho_fs_adapter_posix_la_SOURCES=ldm_fs_posix.c ldm_common.c
libpho_fs_adapter_posix_la_CFLAGS=-fPIC $(AM_CFLAGS)
libpho_fs_adapter_posix_la_LIBADD=../common/libpho_common.la
libpho_fs_adapter_posix_la_LDFLAGS=-version-info 0:0:0

libpho_fs_adapter_ltfs_la_SOURCES=ldm_fs_ltfs.c ldm_common.c
libpho_fs_adapter_ltfs_la_CFLAGS=-fPIC $(AM_CFLAGS)
libpho_fs_adapter_ltfs_la_LIBADD=../common/libpho_common.la \
                                  ../cfg/libpho_cfg.la
libpho_fs_adapter_ltfs_la_LDFLAGS=-version-info 0:0:0

if RADOS_ENABLED
pkglib_LTLIBRARIES+=libpho_dev_adapter_rados_pool.la

libpho_dev_adapter_rados_pool_la_SOURCES=ldm_dev_rados_pool.c
libpho_dev_adapter_rados_pool_la_CFLAGS=-fPIC $(AM_CFLAGS)
libpho_dev_adapter_rados_pool_la_LIBADD=../common/libpho_common.la \
                                        ../cfg/libpho_cfg.la -lrados
libpho_dev_adapter_rados_pool_la_LDFLAGS=-version-info 0:0:0
endif
