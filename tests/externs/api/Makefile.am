AUTOMAKE_OPTIONS=subdir-objects

TO_SRC=../../../src/
TO_TEST=../../

AM_CFLAGS=$(CC_OPT) @CMOCKA_CFLAGS@
AM_LDFLAGS=@CMOCKA_LIBS@

ADMIN_LIB=$(TO_SRC)/admin/libphobos_admin.la
DSS_LIB=$(TO_SRC)/dss/libpho_dss.la
STORE_LIB=$(TO_SRC)/store/libphobos_store.la
LAYOUT_LIB=$(TO_SRC)/layout-modules/libpho_layout_raid1.la

if VALGRIND_ENABLED
@VALGRIND_CHECK_RULES@
VALGRIND_SUPPRESSIONS_FILES=$(TO_TEST)/supp-files/valgrind.supp $(TO_TEST)/supp-files/glib.supp
VALGRIND_FLAGS=-q --leak-check=full --errors-for-leak-kinds=definite --show-possibly-lost=no
endif

TEST_EXTENSIONS=.sh

check_PROGRAMS=test_locate

check_SCRIPTS=setup_locate.sh

TESTS=$(check_SCRIPTS)

test_locate_SOURCES=test_locate.c ../../test_setup.c $(TO_TEST)/test_setup.h
test_locate_LDADD=$(STORE_LIB) $(ADMIN_LIB) $(DSS_LIB) $(LAYOUT_LIB)
test_locate_CFLAGS=$(AM_CFLAGS) -I$(TO_SRC)/dss -I$(TO_TEST)
