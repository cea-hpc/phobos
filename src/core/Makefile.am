AUTOMAKE_OPTIONS=subdir-objects

SUBDIRS=serializer .

pkglib_LTLIBRARIES=libpho_core.la

CFG_SRC=cfg/cfg.c cfg/compatibility.c cfg/cfg_copy.c
COMMON_SRC=common/attrs.c common/common.c common/global_state.c common/log.c \
        common/pho_cache.c common/pho_ref.c common/saj.c common/slist.c \
        common/type_utils.c
COMM_SRC=communication/comm.c communication/comm_wrapper.c
DSS_SRC=dss/dss.c dss/dss_lock.c dss/logs.c dss/dss_utils.c dss/resources.c \
        dss/device.c dss/dss_config.c dss/media.c dss/filters.c dss/extent.c \
        dss/deprecated.c dss/object.c dss/layout.c dss/full_layout.c \
        dss/wrapper.c dss/copy.c
DSS_SUBHEADERS=dss/dss_lock.h dss/logs.h dss/dss_utils.h dss/resources.h \
        dss/device.h dss/dss_config.h dss/media.h dss/filters.h dss/extent.h \
        dss/deprecated.h dss/object.h dss/layout.h dss/full_layout.h \
        dss/copy.h
SERIALIZER_SRC=serializer/proto_common.pb-c.c serializer/proto_lrs.pb-c.c \
        serializer/proto_tlc.pb-c.c serializer/srl_lrs.c \
        serializer/srl_tlc.c serializer/pho_proto_common.proto \
        serializer/pho_proto_lrs.proto serializer/pho_proto_tlc.proto


libpho_core_la_SOURCES=${CFG_SRC} ${COMMON_SRC} ${COMM_SRC} ${DSS_SRC} \
        ${DSS_SUBHEADERS} ${SERIALIZER_SRC}
libpho_core_la_CFLAGS=${LIBPQ_CFLAGS} ${AM_CFLAGS}
libpho_core_la_LDFLAGS=-version-info 0:0:0
libpho_core_la_LDFLAGS+=-lini_config -ljansson -lm -luuid ${LIBPQ_LIBS} \
                       -lprotobuf-c
