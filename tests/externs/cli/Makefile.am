AUTOMAKE_OPTIONS=subdir-objects

TO_SRC=../../../src/
TO_TEST=../../

AM_CFLAGS=$(CC_OPT) @CMOCKA_CFLAGS@
AM_LDFLAGS=@CMOCKA_LIBS@

CFG_LIB=$(TO_SRC)/cfg/libpho_cfg.la
COMMON_LIB=$(TO_SRC)/common/libpho_common.la
COMMUNICATION_LIB=$(TO_SRC)/communication/libpho_comm.la
DSS_LIB=$(TO_SRC)/dss/libpho_dss.la
LRS_LIB=$(TO_SRC)/lrs/libpho_lrs.la
SERIALIZER_LIB=$(TO_SRC)/serializer/libpho_serializer.la
STORE_LIB=$(TO_SRC)/store/libphobos_store.la

if VALGRIND_ENABLED
@VALGRIND_CHECK_RULES@
VALGRIND_SUPPRESSIONS_FILES=$(TO_TEST)/supp-files/valgrind.supp $(TO_TEST)/supp-files/glib.supp
VALGRIND_FLAGS=-q --leak-check=full --errors-for-leak-kinds=definite --show-possibly-lost=no
endif

TEST_EXTENSIONS=.sh

check_PROGRAMS=controlled_store \
               medium_locker

check_SCRIPTS=acceptance.sh \
              test_daemon.sh \
              test_daemon_monitoring.sh \
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
              test_resource_management.sh \
              test_undelete.sh \
              test_lock_clean.sh

TESTS=$(check_SCRIPTS)

controlled_store_SOURCES=controlled_store.c
controlled_store_LDADD=$(LRS_LIB) $(CFG_LIB) $(COMMON_LIB) \
                          $(COMMUNICATION_LIB) $(SERIALIZER_LIB)
controlled_store_CFLAGS=$(AM_CFLAGS) -I$(TO_SRC)/lrs

medium_locker_SOURCES=medium_locker.c
medium_locker_LDADD=$(DSS_LIB) $(STORE_LIB)
medium_locker_CFLAGS=$(AM_CFLAGS) -I$(TO_SRC)/dss
