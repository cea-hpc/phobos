AUTOMAKE_OPTIONS=subdir-objects

TO_SRC=../../../src/
TO_TEST=../../

AM_CFLAGS=$(CC_OPT) @CMOCKA_CFLAGS@
AM_LDFLAGS=@CMOCKA_LIBS@

ADMIN_LIB=$(TO_SRC)/admin/libphobos_admin.la
DSS_LIB=$(TO_SRC)/dss/libpho_dss.la
STORE_LIB=$(TO_SRC)/store/libphobos_store.la

if VALGRIND_ENABLED
@VALGRIND_CHECK_RULES@
VALGRIND_SUPPRESSIONS_FILES=$(TO_TEST)/supp-files/valgrind.supp $(TO_TEST)/supp-files/glib.supp
VALGRIND_FLAGS=-q --leak-check=full --errors-for-leak-kinds=definite --show-possibly-lost=no
endif

TEST_EXTENSIONS=.sh

check_PROGRAMS=medium_locker

check_SCRIPTS=test_delete.sh test_undelete.sh test_put.sh test_object_list.sh \
	          test_extent_list.sh test_ping.sh test_overwrite.sh \
	          test_locate.sh test_get.sh test_resource_availability.sh \
	          test_tag_update.sh test_media_operation_flags.sh

TESTS=$(check_SCRIPTS)

medium_locker_SOURCES=medium_locker.c
medium_locker_LDADD=$(DSS_LIB) $(STORE_LIB)
medium_locker_CFLAGS=$(AM_CFLAGS) -I$(TO_SRC)/dss
