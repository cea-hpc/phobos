AM_CFLAGS= $(CC_OPT)

noinst_LTLIBRARIES=libpho_ldm.la libpho_scsi.la

libpho_scsi_la_SOURCES=scsi_common.h scsi_common.c

libpho_ldm_la_SOURCES=ldm.c
libpho_ldm_la_LIBADD=libpho_scsi.la
