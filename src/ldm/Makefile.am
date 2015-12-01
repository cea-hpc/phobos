AM_CFLAGS= $(CC_OPT)

noinst_LTLIBRARIES=libpho_ldm.la libpho_scsi.la

noinst_HEADERS=scsi_common.h ldm_common.h

libpho_scsi_la_SOURCES=scsi_common.c scsi_api.h scsi_api.c

libpho_ldm_la_SOURCES=ldm.c lintape_lookup.c ldm_common.c ldm_lib_scsi.h ldm_lib_scsi.c
libpho_ldm_la_LIBADD=libpho_scsi.la
