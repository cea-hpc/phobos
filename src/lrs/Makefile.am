AM_CFLAGS= $(CC_OPT)

sbin_PROGRAMS=phobosd

noinst_LTLIBRARIES=libpho_lrs.la

unitdir=`pkg-config systemd --variable=systemdsystemunitdir`
unit_files=phobosd.service
unit_DATA=$(unit_files)
EXTRA_DIST=$(unit_files)

IO_SCHEDULERS=io_schedulers/fifo.c \
              io_schedulers/device_dispatch_algorithms.c \
              io_schedulers/schedulers.h \
              io_schedulers/scheduler_priority_algorithms.c

phobosd_SOURCES=lrs.c \
                lrs_cfg.h lrs_cfg.c \
                lrs_device.h lrs_device.c \
                lrs_sched.h lrs_sched.c \
                lrs_thread.h lrs_thread.c \
                lrs_utils.h lrs_utils.c \
                io_sched.h io_sched.c \
                $(IO_SCHEDULERS)

phobosd_CFLAGS=$(AM_CFLAGS)
phobosd_LDADD=../dss/libpho_dss.la \
              ../cfg/libpho_cfg.la \
              ../ldm/libpho_ldm.la \
              ../module-loader/libpho_module_loader.la \
              ../io/libpho_io.la \
              ../serializer/libpho_serializer.la \
              ../communication/libpho_comm.la \
              ../common/libpho_common.la
phobosd_LDFLAGS=-Wl,-rpath=$(libdir) -Wl,-rpath=$(pkglibdir)

libpho_lrs_la_SOURCES=lrs_cfg.c lrs_sched.c lrs_device.c lrs_thread.c \
                      io_sched.c lrs_utils.c $(IO_SCHEDULERS)
