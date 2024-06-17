AUTOMAKE_OPTIONS=subdir-objects

TO_SRC=../../../src/
TO_TEST=../../

AM_CFLAGS=$(CC_OPT) @CMOCKA_CFLAGS@
AM_LDFLAGS=@CMOCKA_LIBS@

ADMIN_LIB=$(TO_SRC)/admin/libphobos_admin.la
CFG_LIB=$(TO_SRC)/cfg/libpho_cfg.la
COMMON_LIB=$(TO_SRC)/common/libpho_common.la
COMMUNICATION_LIB=$(TO_SRC)/communication/libpho_comm.la
DSS_LIB=$(TO_SRC)/dss/libpho_dss.la
LRS_LIB=$(TO_SRC)/lrs/libpho_lrs.la
SERIALIZER_LIB=$(TO_SRC)/serializer/libpho_serializer.la
STORE_LIB=$(TO_SRC)/store/libphobos_store.la
SCSI_LIB=$(TO_SRC)/tlc/scsi/libpho_scsi.la
TLC_LIB=$(TO_SRC)/tlc/libpho_tlc.la

if VALGRIND_ENABLED
# FIXME:
# Possibly-lost leaks appear in valgrind reports when testing
# externs/cli/acceptance.sh, generating millions of lines (each phobos command
# execution leads to ~5k leaks). Those leaks are somewhat bound to the python
# CLI, and are not clear enough to be resolve for now.
#
# I made some research and there is a way to get better reports and then try to
# resolve those leaks:
# - get an environment where python is compiled in debug mode to let valgrind
#   catch more information
#   $ ./configure --with-pydebug --without-pymalloc --with-valgrind

@VALGRIND_CHECK_RULES@
VALGRIND_SUPPRESSIONS_FILES=$(TO_TEST)/supp-files/valgrind.supp $(TO_TEST)/supp-files/glib.supp
VALGRIND_FLAGS=-q --leak-check=full --errors-for-leak-kinds=definite --show-possibly-lost=no
endif

TEST_EXTENSIONS=.sh
TEST_EXTENSIONS+=.test
TEST_LOG_DRIVER=$(TO_TEST)/test-driver.sh

check_PROGRAMS=test_fair_share \
               lrs_simple_client \
               medium_locker

check_SCRIPTS=acceptance.test \
              test_compatible_drive_exists.test \
              test_delete.sh \
              test_drive_lookup.test \
              test_extent_list.sh \
              test_fair_share.test \
              test_file_after_put.test \
              test_format.sh \
              test_get.sh \
              test_group_sync.sh \
              test_import.test \
              test_ldm.sh \
              test_locate.test \
              test_lock_clean.sh \
              test_logs.test \
              test_lrs.test \
              test_lrs_drive_status.sh \
              test_lrs_scheduling.test \
              test_media.sh \
              test_multilibrary.test \
              test_object_list.sh \
              test_phobos_tape_library_test.sh \
              test_ping.test \
              test_put.sh \
              test_raid1_split.sh \
              test_raid4.test \
              test_repack.test \
              test_resource_availability.test \
              test_resource_management.sh \
              test_tlc.test \
              test_tlc_load_unload.test \
              test_tlc_status_refresh.test \
              test_undelete.sh \
              test_unload_with_source_set_to_drive.test \
              test_unlock_at_unload.test

if RADOS_ENABLED
check_SCRIPTS+=test_store_rados_commands.sh test_locate_rados.test
endif

TESTS_ENVIRONMENT = HAVE_XXH128="@HAVE_XXH128@"
TESTS=$(check_SCRIPTS)

test_fair_share_SOURCES=test_fair_share.c
test_fair_share_LDADD=$(DSS_LIB) $(COMMON_LIB) $(CFG_LIB) $(COMMUNICATION_LIB) \
                      $(SERIALIZER_LIB) $(LRS_LIB)
test_fair_share_CFLAGS=-I$(TO_TEST)

lrs_simple_client_SOURCES=lrs_simple_client.c
lrs_simple_client_LDADD=$(LRS_LIB) $(CFG_LIB) $(COMMON_LIB) \
                        $(COMMUNICATION_LIB) $(SERIALIZER_LIB)
lrs_simple_client_CFLAGS=$(AM_CFLAGS) -I$(TO_SRC)/lrs

medium_locker_SOURCES=medium_locker.c ../../test_setup.c
medium_locker_LDADD=$(TLC_LIB) $(SCSI_LIB) $(ADMIN_LIB) $(DSS_LIB) $(STORE_LIB)
medium_locker_CFLAGS=$(AM_CFLAGS) -I$(TO_SRC)/tlc -I$(TO_SRC)/tlc/scsi \
                     -I$(TO_SRC)/dss -I$(TO_TEST)
