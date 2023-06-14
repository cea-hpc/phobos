AM_CFLAGS= $(CC_OPT)

noinst_LTLIBRARIES=libpho_dss.la

libpho_dss_la_SOURCES=dss.c dss_lock.c dss_lock.h dss_logs.c \
                      dss_utils.c dss_utils.h
libpho_dss_la_CFLAGS=${LIBPQ_CFLAGS} ${AM_CFLAGS}
libpho_dss_la_LIBADD=${LIBPQ_LIBS}
