AM_CFLAGS= $(CC_OPT)

noinst_LTLIBRARIES=libpho_ldm.la

noinst_HEADERS=ldm_common.h ldm_dev_adapters.h

libpho_ldm_la_SOURCES=ldm.c ldm_common.c ldm_dev_dir.c ldm_dev_scsi_tape.c \
                      ldm_fs_posix.c ldm_fs_ltfs.c
libpho_ldm_la_LIBADD=-ldl

if RADOS_ENABLED
libpho_ldm_la_SOURCES+=ldm_dev_rados_pool.c
libpho_ldm_la_LIBADD+=-lrados
endif
