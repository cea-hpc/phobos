AM_CFLAGS=$(CC_OPT) @CMOCKA_CFLAGS@
AM_LDFLAGS=@CMOCKA_LIBS@

TESTS=test_dss_lock test_dss_object_move test_ping test_store_object_md
check_PROGRAMS=$(TESTS)

noinst_PROGRAMS=$(check_PROGRAMS)
noinst_HEADERS=test_setup.h

test_dss_lock_SOURCES=test_dss_lock.c test_setup.c
test_dss_lock_LDADD=../../dss/libpho_dss.la ../../cfg/libpho_cfg.la \
                    ../../common/libpho_common.la

test_dss_object_move_SOURCES=test_dss_object_move.c test_setup.c
test_dss_object_move_LDADD=../../dss/libpho_dss.la ../../cfg/libpho_cfg.la \
                           ../../common/libpho_common.la

test_ping_SOURCES=test_ping.c mock_communication.c
test_ping_LDADD=../../admin/libphobos_admin.la ../../dss/libpho_dss.la \
                ../../common/libpho_common.la ../../ldm/libpho_ldm.la

test_store_object_md_SOURCES=test_store_object_md.c
test_store_object_md_LDADD=../../store/libphobos_store.la
