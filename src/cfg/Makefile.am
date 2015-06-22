AM_CFLAGS= $(CC_OPT)

noinst_LTLIBRARIES=libpho_cfg.la

libpho_cfg_la_SOURCES=cfg.c
# for now, CFG doesn't rely on DSS, but in the future, it will.
#libpho_cfg_la_LIBADD=../dss/libpho_dss.la -lini_config
libpho_cfg_la_LIBADD=../common/libpho_common.la -lini_config
