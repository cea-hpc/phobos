AM_CFLAGS= $(CC_OPT)

sbin_PROGRAMS=phobosd

unitdir=`pkg-config systemd --variable=systemdsystemunitdir`
unit_files=phobosd.service
unit_DATA=$(unit_files)
EXTRA_DIST=$(unit_files)

phobosd_SOURCES=lrs.c lrs_cfg.h lrs_cfg.c lrs_sched.h lrs_sched.c
phobosd_CFLAGS=$(AM_CFLAGS)
phobosd_LDADD=../dss/libpho_dss.la ../cfg/libpho_cfg.la ../ldm/libpho_ldm.la \
	      ../io/libpho_io.la ../serializer/libpho_serializer.la \
	      ../communication/libpho_comm.la ../common/libpho_common.la
phobosd_LDFLAGS=-Wl,-rpath=$(libdir) -Wl,-rpath=$(pkglibdir)
