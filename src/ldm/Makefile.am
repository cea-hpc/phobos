AM_CFLAGS= $(CC_OPT)

noinst_LTLIBRARIES=libpho_ldm.la libpho_scsi.la

noinst_HEADERS=scsi_common.h ldm_common.h scsi_api.h

libpho_scsi_la_SOURCES=scsi_common.c scsi_api.c

libpho_ldm_la_SOURCES=ldm.c ldm_common.c ldm_lib_scsi.c ldm_lib_dummy.c \
		      ldm_dev_dir.c ldm_dev_lintape.c ldm_fs_posix.c \
		      ldm_fs_ltfs.c
libpho_ldm_la_LIBADD=libpho_scsi.la
