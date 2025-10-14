AUTOMAKE_OPTIONS=subdir-objects

TO_SRC=../../../src/
TO_TEST=../../

AM_CFLAGS=$(CC_OPT) @CMOCKA_CFLAGS@
AM_LDFLAGS=@CMOCKA_LIBS@

ADMIN_LIB=$(TO_SRC)/admin/libphobos_admin.la
CORE_LIB=$(TO_SRC)/core/libpho_core.la
LRS_LIB=$(TO_SRC)/lrs/libpho_lrs.la
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
              test_client_disconnection.test \
              test_compatible_drive_exists.test \
              test_copy_create.test \
              test_copy_delete.test \
              test_copy_grouping.test \
              test_copy_list.test \
              test_ctime_ordered_read.test \
              test_delete.test \
              test_drive_list.test \
              test_drive_lookup.test \
              test_drive_migrate.test \
              test_drive_scsi_release.test \
              test_extent_list.test \
              test_fair_share.test \
              test_fifo_max_write_per_grouping.test \
              test_file_after_put.test \
              test_format.test \
              test_get.test \
              test_group_sync.sh \
              test_grouping.test \
              test_hsm_sync_dir.test \
              test_hsm_release_dir.test \
              test_import.test \
              test_ldm.sh \
              test_list_sort.test \
              test_locate.test \
              test_lock_clean.sh \
              test_logs.test \
              test_lost.test \
              test_lrs.test \
              test_lrs_drive_status.test \
              test_lrs_scheduling.test \
              test_media.test \
              test_media_delete.test \
              test_multilibrary.test \
              test_mput_no_split.test \
              test_object_list.test \
              test_phobos_tape_library_test.sh \
              test_ping.test \
              test_put.test \
              test_raid1.test \
              test_raid4.test \
              test_rename.test \
              test_repack.test \
              test_resource_availability.test \
              test_resource_management.sh \
              test_tlc.test \
              test_tlc_load_unload.test \
              test_tlc_status_refresh.test \
              test_undelete.test \
              test_unload_with_source_set_to_drive.test \
              test_unlock_at_unload.test

if RADOS_ENABLED
check_SCRIPTS+=test_store_rados_commands.sh test_locate_rados.test \
               test_media_delete_rados.test
endif

TESTS_ENVIRONMENT = HAVE_XXH128="@HAVE_XXH128@"
TESTS=$(check_SCRIPTS)

test_fair_share_SOURCES=test_fair_share.c
test_fair_share_LDADD=$(CORE_LIB) $(LRS_LIB)
test_fair_share_CFLAGS=-I$(TO_TEST)

lrs_simple_client_SOURCES=lrs_simple_client.c
lrs_simple_client_LDADD=$(LRS_LIB) $(CORE_LIB)
lrs_simple_client_CFLAGS=$(AM_CFLAGS) -I$(TO_SRC)/lrs

medium_locker_SOURCES=medium_locker.c ../../test_setup.c
medium_locker_LDADD=$(TLC_LIB) $(SCSI_LIB) $(ADMIN_LIB) $(CORE_LIB) $(STORE_LIB)
medium_locker_CFLAGS=$(AM_CFLAGS) -I$(TO_SRC)/tlc -I$(TO_SRC)/tlc/scsi \
                     -I$(TO_SRC)/core/dss -I$(TO_TEST)
