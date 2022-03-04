AUTOMAKE_OPTIONS=subdir-objects

TO_SRC=../../../src/
TO_TEST=../../

AM_CFLAGS=$(CC_OPT) @CMOCKA_CFLAGS@
AM_LDFLAGS=@CMOCKA_LIBS@

DSS_LIB=$(TO_SRC)/dss/libpho_dss.la
STORE_LIB=$(TO_SRC)/store/libphobos_store.la

if VALGRIND_ENABLED
@VALGRIND_CHECK_RULES@
VALGRIND_SUPPRESSIONS_FILES=$(TO_TEST)/supp-files/valgrind.supp $(TO_TEST)/supp-files/glib.supp
VALGRIND_FLAGS=-q --leak-check=full --errors-for-leak-kinds=definite --show-possibly-lost=no
endif

TEST_EXTENSIONS=.sh

check_PROGRAMS=medium_locker

check_SCRIPTS=acceptance.sh \
              test_daemon.sh \
              test_daemon_scheduling.sh \
              test_delete.sh \
              test_extent_list.sh \
              test_get.sh \
              test_group_sync.sh \
              test_ldm.sh \
              test_locate.sh \
              test_media.sh \
              test_object_list.sh \
              test_ping.sh \
              test_put.sh \
              test_raid1_split.sh \
              test_resource_availability.sh \
              test_undelete.sh

TESTS=$(check_SCRIPTS)

medium_locker_SOURCES=medium_locker.c
medium_locker_LDADD=$(DSS_LIB) $(STORE_LIB)
medium_locker_CFLAGS=$(AM_CFLAGS) -I$(TO_SRC)/dss
