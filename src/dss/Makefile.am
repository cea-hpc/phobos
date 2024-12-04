AM_CFLAGS= $(CC_OPT)

noinst_LTLIBRARIES=libpho_dss.la

libpho_dss_la_SOURCES=dss.c dss_lock.c dss_lock.h \
                      logs.h logs.c dss_utils.c dss_utils.h \
                      resources.c resources.h device.c device.h dss_config.h \
                      dss_config.c media.c media.h filters.c filters.h \
                      extent.c extent.h deprecated.c deprecated.h \
                      object.c object.h layout.c layout.h full_layout.c \
                      full_layout.h wrapper.c copy.c copy.h
libpho_dss_la_CFLAGS=${LIBPQ_CFLAGS} ${AM_CFLAGS}
libpho_dss_la_LIBADD=${LIBPQ_LIBS}
