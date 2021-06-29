AM_CFLAGS=$(CC_OPT) @CMOCKA_CFLAGS@
AM_LDFLAGS=@CMOCKA_LIBS@

if VALGRIND_ENABLED
@VALGRIND_CHECK_RULES@
VALGRIND_SUPPRESSIONS_FILES=../supp-files/valgrind.supp ../supp-files/glib.supp
VALGRIND_FLAGS=-q --leak-check=full --errors-for-leak-kinds=definite --show-possibly-lost=no
endif

TEST_EXTENSIONS=.sh

check_PROGRAMS=test_dss_lock test_dss_object_move test_ping \
	           test_dss_lazy_find_object \
	           test_dss_medium_locate test_phobos_admin_medium_locate \
	           test_store_object_md_get
#test_store_object_md

TESTS=$(check_PROGRAMS)

test_dss_lock_SOURCES=test_dss_lock.c ../test_setup.c ../test_setup.h
test_dss_lock_LDADD=../../dss/libpho_dss.la ../../cfg/libpho_cfg.la \
                    ../../common/libpho_common.la  \
                    ../../admin/libphobos_admin.la ../../ldm/libpho_ldm.la

test_dss_object_move_SOURCES=test_dss_object_move.c ../test_setup.c
	                         ../test_setup.h
test_dss_object_move_LDADD=../../dss/libpho_dss.la ../../cfg/libpho_cfg.la \
                           ../../common/libpho_common.la \
                           ../../admin/libphobos_admin.la \
                           ../../ldm/libpho_ldm.la

test_ping_SOURCES=test_ping.c mock_communication.c
test_ping_LDADD=../../admin/libphobos_admin.la ../../dss/libpho_dss.la \
                ../../common/libpho_common.la ../../ldm/libpho_ldm.la

test_store_object_md_get_SOURCES=test_store_object_md_get.c ../test_setup.c
test_store_object_md_get_LDADD=../../store/libphobos_store.la \
	                           ../../admin/libphobos_admin.la

#test_store_object_md_SOURCES=test_store_object_md.c
#test_store_object_md_LDADD=../../store/libphobos_store.la

test_dss_lazy_find_object_SOURCES=test_dss_lazy_find_object.c ../test_setup.c \
	                              ../test_setup.h
test_dss_lazy_find_object_LDADD=../../dss/libpho_dss.la \
                                ../../cfg/libpho_cfg.la \
                                ../../common/libpho_common.la \
                                ../../admin/libphobos_admin.la \
                                ../../ldm/libpho_ldm.la

test_dss_medium_locate_SOURCES=test_dss_medium_locate.c ../test_setup.c \
	                           ../test_setup.h
test_dss_medium_locate_LDADD=../../dss/libpho_dss.la ../../cfg/libpho_cfg.la \
                             ../../common/libpho_common.la \
                             ../../admin/libphobos_admin.la \
                             ../../ldm/libpho_ldm.la

test_phobos_admin_medium_locate_SOURCES=test_phobos_admin_medium_locate.c \
                                        ../test_setup.c ../test_setup.h
test_phobos_admin_medium_locate_LDADD=../../admin/libphobos_admin.la \
                                      ../../dss/libpho_dss.la \
                                      ../../cfg/libpho_cfg.la \
                                      ../../common/libpho_common.la \
                                      ../../ldm/libpho_ldm.la
