AM_CFLAGS= $(CC_OPT)

noinst_LTLIBRARIES=libpho_dss.la

libpho_dss_la_SOURCES=dss.c dss_action_update.c dss_lock.c dss_lock.h \
                      dss_logs.h dss_logs.c dss_utils.c dss_utils.h \
                      resources.c resources.h device.c device.h dss_config.h \
                      dss_config.c
libpho_dss_la_CFLAGS=${LIBPQ_CFLAGS} ${AM_CFLAGS}
libpho_dss_la_LIBADD=${LIBPQ_LIBS}
