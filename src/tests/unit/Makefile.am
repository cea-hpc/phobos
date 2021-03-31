AM_CFLAGS=$(CC_OPT) @CMOCKA_CFLAGS@
AM_LDFLAGS=@CMOCKA_LIBS@

TESTS=test_dss_lock test_ping
check_PROGRAMS=$(TESTS)

test_dss_lock_SOURCES=test_dss_lock.c
test_dss_lock_LDADD=../../dss/libpho_dss.la ../../cfg/libpho_cfg.la \
                    ../../common/libpho_common.la

test_ping_SOURCES=test_ping.c mock_communication.c
test_ping_LDADD=../../admin/libphobos_admin.la ../../dss/libpho_dss.la \
                ../../common/libpho_common.la ../../ldm/libpho_ldm.la
