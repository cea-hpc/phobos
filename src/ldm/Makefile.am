AM_CFLAGS= $(CC_OPT)

noinst_LTLIBRARIES=libpho_ldm.la libpho_scsi.la

libpho_scsi_la_SOURCES=scsi_common.h scsi_common.c scsi_api.h scsi_api.c

libpho_ldm_la_SOURCES=ldm.c lintape_lookup.c
libpho_ldm_la_LIBADD=libpho_scsi.la
