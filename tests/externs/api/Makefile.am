AUTOMAKE_OPTIONS=subdir-objects

TO_SRC=../../../src/
TO_TEST=../../

AM_CFLAGS=$(CC_OPT) @CMOCKA_CFLAGS@
AM_LDFLAGS=@CMOCKA_LIBS@

ADMIN_LIB=$(TO_SRC)/admin/libphobos_admin.la
CORE_LIB=$(TO_SRC)/core/libpho_core.la
LAYOUT_COMMON_LIB=$(TO_SRC)/layout/libpho_layout_common.la
LDM_LIB=$(TO_SRC)/ldm/libpho_ldm.la
LRS_LIB=$(TO_SRC)/lrs/libpho_lrs.la
MOD_LOAD_LIB=$(TO_SRC)/module-loader/libpho_module_loader.la
RAID1_LIB=$(TO_SRC)/layout-modules/libpho_layout_raid1.la
SCSI_LIB=$(TO_SRC)/tlc/scsi/libpho_scsi.la
STORE_LIB=$(TO_SRC)/store/libphobos_store.la
IO_LIB=$(TO_SRC)/io/libpho_io.la

TESTS_LIB_DEPS=$(LRS_LIB) $(LDM_LIB) $(IO_LIB) $(CORE_LIB)
TESTS_LIB=../../libpho_test_utils.la

TESTS_INCLUDE=-I$(TO_SRC)/core/dss -I$(TO_SRC)/tlc -I$(TO_SRC)/tlc/scsi \
        -I$(TO_TEST)

if VALGRIND_ENABLED
@VALGRIND_CHECK_RULES@
VALGRIND_SUPPRESSIONS_FILES=$(TO_TEST)/supp-files/valgrind.supp $(TO_TEST)/supp-files/glib.supp
VALGRIND_FLAGS=-q --leak-check=full --errors-for-leak-kinds=definite --show-possibly-lost=no
endif

TEST_EXTENSIONS=.sh
TEST_EXTENSIONS+=.test
TEST_LOG_DRIVER=$(TO_TEST)/test-driver.sh

check_PROGRAMS=phobos_put_copy \
               test_bad_comm \
               test_concurrency \
               test_delete \
               test_dss \
               test_locate \
               test_lock_clean \
               test_mput_sync_error \
               test_multithread \
               test_raid1_split_locate \
               test_scsi \
               test_store \
               test_store_retry \
               test_undelete

check_SCRIPTS=test_bad_comm.sh \
              test_concurrency.test \
              test_delete.test \
              test_dss.sh \
              test_store.test \
              test_locate.test \
              test_lock_clean.sh \
              test_mput_sync_error.test \
              test_multithread.test \
              test_put_copy.test \
              test_raid1_split_locate.sh \
              test_scsi.test \
              test_store_retry.sh \
              test_undelete.sh

if RADOS_ENABLED
check_PROGRAMS+=test_lib_dev_rados test_io_rados test_fs_rados
check_SCRIPTS+=test_lib_dev_rados.sh test_io_rados.sh test_fs_rados.sh
endif

TESTS=$(check_SCRIPTS)

phobos_put_copy_SOURCES=phobos_put_copy.c
phobos_put_copy_LDADD=$(STORE_LIB) $(TESTS_LIB) $(TESTS_LIB_DEPS)
phobos_put_copy_CFLAGS=$(TESTS_INCLUDE)

test_bad_comm_SOURCES=test_bad_comm.c
test_bad_comm_LDADD=$(CORE_LIB) $(TESTS_LIB) $(TESTS_LIB_DEPS)
test_bad_comm_CFLAGS=$(TESTS_INCLUDE)

test_concurrency_SOURCES=test_concurrency.c
test_concurrency_LDADD=$(STORE_LIB)

test_delete_SOURCES=test_delete.c
test_delete_LDADD=$(STORE_LIB) $(TESTS_LIB) $(TESTS_LIB_DEPS)
test_delete_CFLAGS=$(TESTS_INCLUDE)

test_dss_SOURCES=test_dss.c
test_dss_LDADD=$(ADMIN_LIB) $(TESTS_LIB) $(TESTS_LIB_DEPS)
test_dss_CFLAGS=$(TESTS_INCLUDE)

test_locate_SOURCES=test_locate.c
test_locate_LDADD=$(STORE_LIB) $(ADMIN_LIB) $(RAID1_LIB) \
                  $(TESTS_LIB) $(TESTS_LIB_DEPS)
test_locate_CFLAGS=$(AM_CFLAGS) $(TESTS_INCLUDE)

test_lock_clean_SOURCES=test_lock_clean.c
test_lock_clean_LDADD=$(ADMIN_LIB) $(STORE_LIB) $(TESTS_LIB) $(TESTS_LIB_DEPS)
test_lock_clean_CFLAGS=$(TESTS_INCLUDE)

test_mput_sync_error_SOURCES=test_mput_sync_error.c
test_mput_sync_error_LDADD=$(STORE_LIB) $(LAYOUT_COMMON_LIB) \
                           $(TESTS_LIB) $(TESTS_LIB_DEPS)
test_mput_sync_error_CFLAGS=$(AM_CFLAGS) $(TESTS_INCLUDE)
if USE_XXHASH
test_mput_sync_error_LDFLAGS=-lxxhash
endif

test_multithread_SOURCES=test_multithread.c
test_multithread_LDADD=$(STORE_LIB) $(TESTS_LIB) $(TESTS_LIB_DEPS)
test_multithread_LDFLAGS=-pthread
test_multithread_CFLAGS=$(TESTS_INCLUDE)

if RADOS_ENABLED
test_lib_dev_rados_SOURCES=test_lib_dev_rados.c
test_lib_dev_rados_LDADD=$(ADMIN_LIB) $(LDM_LIB)
test_lib_dev_rados_CFLAGS=$(TESTS_INCLUDE)

test_io_rados_SOURCES=test_io_rados.c
test_io_rados_LDADD=$(ADMIN_LIB) $(IO_LIB) $(LDM_LIB) -lrados
test_io_rados_CFLAGS=$(TESTS_INCLUDE)

test_fs_rados_SOURCES=test_fs_rados.c
test_fs_rados_LDADD=$(ADMIN_LIB) $(LDM_LIB) -lrados
test_fs_rados_CFLAGS=$(TESTS_INCLUDE)
endif

test_raid1_split_locate_SOURCES=test_raid1_split_locate.c
test_raid1_split_locate_LDADD=$(ADMIN_LIB) $(RAID1_LIB) \
                              $(TESTS_LIB) $(TESTS_LIB_DEPS)
test_raid1_split_locate_CFLAGS=$(TESTS_INCLUDE)

test_scsi_SOURCES=test_scsi.c
test_scsi_LDADD=$(SCSI_LIB) $(ADMIN_LIB) $(MOD_LOAD_LIB) \
                $(TESTS_LIB) $(TESTS_LIB_DEPS)
test_scsi_CFLAGS=$(TESTS_INCLUDE)

test_store_SOURCES=test_store.c
test_store_LDADD=$(STORE_LIB) $(TESTS_LIB) $(TESTS_LIB_DEPS)
test_store_CFLAGS=$(TESTS_INCLUDE)

test_store_retry_SOURCES=test_store_retry.c
test_store_retry_LDADD=$(ADMIN_LIB) $(STORE_LIB) $(TESTS_LIB) $(TESTS_LIB_DEPS)
test_store_retry_CFLAGS=$(TESTS_INCLUDE)

test_undelete_SOURCES=test_undelete.c
test_undelete_LDADD=$(STORE_LIB) $(TESTS_LIB) $(TESTS_LIB_DEPS)
test_undelete_CFLAGS=$(TESTS_INCLUDE)
