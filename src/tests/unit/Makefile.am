AM_CFLAGS=$(CC_OPT) @CMOCKA_CFLAGS@
AM_LDFLAGS=@CMOCKA_LIBS@

TESTS=test_dss_lock
check_PROGRAMS=$(TESTS)

test_dss_lock_SOURCES=test_dss_lock.c
test_dss_lock_LDADD=../../dss/libpho_dss.la ../../cfg/libpho_cfg.la \
                    ../../common/libpho_common.la

