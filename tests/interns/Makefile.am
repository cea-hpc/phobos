TO_SRC=../../src

AM_CFLAGS=$(CC_OPT) @CMOCKA_CFLAGS@
AM_LDFLAGS=@CMOCKA_LIBS@

ADMIN_LIB=$(TO_SRC)/admin/libphobos_admin.la
CFG_LIB=$(TO_SRC)/cfg/libpho_cfg.la
COMMON_LIB=$(TO_SRC)/common/libpho_common.la
DSS_LIB=$(TO_SRC)/dss/libpho_dss.la
LDM_LIB=$(TO_SRC)/ldm/libpho_ldm.la
STORE_LIB=$(TO_SRC)/store/libphobos_store.la

if VALGRIND_ENABLED
@VALGRIND_CHECK_RULES@
VALGRIND_SUPPRESSIONS_FILES=../supp-files/valgrind.supp ../supp-files/glib.supp
VALGRIND_FLAGS=-q --leak-check=full --errors-for-leak-kinds=definite --show-possibly-lost=no
endif

TEST_EXTENSIONS=.sh

check_PROGRAMS=test_dss_lock test_dss_object_move test_ping \
	           test_dss_lazy_find_object test_store_object_md \
	           test_dss_medium_locate test_phobos_admin_medium_locate \
	           test_store_object_md_get

TESTS=$(check_PROGRAMS)

test_dss_lock_SOURCES=test_dss_lock.c ../test_setup.c ../test_setup.h
test_dss_lock_LDADD=$(DSS_LIB) $(ADMIN_LIB) $(CFG_LIB) $(COMMON_LIB) $(LDM_LIB)
test_dss_lock_CFLAGS=$(AM_CFLAGS) -I$(TO_SRC)/dss

test_dss_object_move_SOURCES=test_dss_object_move.c ../test_setup.c
	                         ../test_setup.h
test_dss_object_move_LDADD=$(DSS_LIB) $(ADMIN_LIB) $(CFG_LIB) $(COMMON_LIB) \
                           $(LDM_LIB)

test_ping_SOURCES=test_ping.c mock_communication.c
test_ping_LDADD=$(ADMIN_LIB) $(COMMON_LIB) $(DSS_LIB) $(LDM_LIB)
test_ping_CFLAGS=$(AM_CFLAGS) -I$(TO_SRC)/admin

test_store_object_md_get_SOURCES=test_store_object_md_get.c ../test_setup.c
test_store_object_md_get_LDADD=$(STORE_LIB) $(ADMIN_LIB)
test_store_object_md_get_CFLAGS=$(AM_CFLAGS) -I$(TO_SRC)/store

test_store_object_md_SOURCES=test_store_object_md.c
test_store_object_md_LDADD=$(STORE_LIB)
test_store_object_md_CFLAGS=$(AM_CFLAGS) -I$(TO_SRC)/dss -I$(TO_SRC)/store

test_dss_lazy_find_object_SOURCES=test_dss_lazy_find_object.c ../test_setup.c \
	                              ../test_setup.h
test_dss_lazy_find_object_LDADD=$(DSS_LIB) $(CFG_LIB) $(COMMON_LIB) \
                                $(ADMIN_LIB) $(LDM_LIB)
test_dss_lazy_find_object_CFLAGS=$(AM_CFLAGS) -I$(TO_SRC)/store

test_dss_medium_locate_SOURCES=test_dss_medium_locate.c ../test_setup.c \
	                           ../test_setup.h
test_dss_medium_locate_LDADD=$(DSS_LIB) $(CFG_LIB) $(COMMON_LIB) $(ADMIN_LIB) \
                             $(LDM_LIB)
test_dss_medium_locate_CFLAGS=$(AM_CFLAGS) -I$(TO_SRC)/dss

test_phobos_admin_medium_locate_SOURCES=test_phobos_admin_medium_locate.c \
                                        ../test_setup.c ../test_setup.h
test_phobos_admin_medium_locate_LDADD=$(ADMIN_LIB) $(DSS_LIB) $(CFG_LIB) \
                                      $(COMMON_LIB) $(LDM_LIB)
test_phobos_admin_medium_locate_CFLAGS=$(AM_CFLAGS) -I$(TO_SRC)/dss
