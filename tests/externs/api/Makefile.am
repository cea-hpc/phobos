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
LAYOUT_LIB=$(TO_SRC)/layout-modules/libpho_layout_raid1.la
LDM_LIB=$(TO_SRC)/ldm/libpho_ldm.la
MOD_LOAD_LIB=$(TO_SRC)/module-loader/libpho_module_loader.la
RAID1_LIB=$(TO_SRC)/layout-modules/libpho_layout_raid1.la
SCSI_LIB=$(TO_SRC)/ldm-modules/libpho_scsi.la
SERIALIZER_LIB=$(TO_SRC)/serializer/libpho_serializer.la
STORE_LIB=$(TO_SRC)/store/libphobos_store.la
IO_LIB=$(TO_SRC)/io/libpho_io.la

if VALGRIND_ENABLED
@VALGRIND_CHECK_RULES@
VALGRIND_SUPPRESSIONS_FILES=$(TO_TEST)/supp-files/valgrind.supp $(TO_TEST)/supp-files/glib.supp
VALGRIND_FLAGS=-q --leak-check=full --errors-for-leak-kinds=definite --show-possibly-lost=no
endif

TEST_EXTENSIONS=.sh

check_PROGRAMS=test_bad_comm \
               test_concurrency \
               test_delete \
               test_dss \
               test_locate \
               test_lock_clean \
               test_raid1_split_locate \
               test_scsi \
               test_store \
               test_store_retry \
               test_undelete

check_SCRIPTS=test_bad_comm.sh \
              test_concurrency.sh \
              test_delete.sh \
              test_dss.sh \
              test_locate.sh \
              test_lock_clean.sh \
              test_raid1_split_locate.sh \
              test_scsi.sh \
              test_store.sh \
              test_store_retry.sh \
              test_undelete.sh

if RADOS_ENABLED
check_PROGRAMS+=test_lib_dev_rados test_io_rados
check_SCRIPTS+=test_lib_dev_rados.sh test_io_rados.sh
endif

TESTS=$(check_SCRIPTS)

test_bad_comm_SOURCES=test_bad_comm.c
test_bad_comm_LDADD=$(COMMON_LIB) $(COMMUNICATION_LIB) $(SERIALIZER_LIB)
test_bad_comm_CFLAGS=-I$(TO_TEST)

test_concurrency_SOURCES=test_concurrency.c
test_concurrency_LDADD=$(STORE_LIB)

test_delete_SOURCES=test_delete.c
test_delete_LDADD=$(STORE_LIB)
test_delete_CFLAGS=-I$(TO_TEST)

test_dss_SOURCES=test_dss.c
test_dss_LDADD=$(DSS_LIB) $(CFG_LIB) $(COMMON_LIB)
test_dss_CFLAGS=-I$(TO_TEST)

test_locate_SOURCES=test_locate.c ../../test_setup.c
test_locate_LDADD=$(STORE_LIB) $(ADMIN_LIB) $(DSS_LIB) $(LAYOUT_LIB)
test_locate_CFLAGS=$(AM_CFLAGS) -I$(TO_SRC)/dss -I$(TO_TEST)

test_lock_clean_SOURCES=test_lock_clean.c ../../test_setup.c
test_lock_clean_LDADD=$(ADMIN_LIB) $(STORE_LIB)
test_lock_clean_CFLAGS=-I$(TO_TEST)

if RADOS_ENABLED
test_lib_dev_rados_SOURCES=test_lib_dev_rados.c
test_lib_dev_rados_LDADD=$(ADMIN_LIB) $(LDM_LIB)
test_lib_dev_rados_CFLAGS=-I$(TO_TEST)

test_io_rados_SOURCES=test_io_rados.c
test_io_rados_LDADD=$(ADMIN_LIB) $(IO_LIB) $(LDM_LIB)
test_io_rados_CFLAGS=-I$(TO_TEST)
endif

test_raid1_split_locate_SOURCES=test_raid1_split_locate.c
test_raid1_split_locate_LDADD=$(CFG_LIB) $(COMMON_LIB) $(DSS_LIB) $(RAID1_LIB)
test_raid1_split_locate_CFLAGS=-I$(TO_TEST)

test_scsi_SOURCES=test_scsi.c
test_scsi_LDADD=$(LDM_LIB) $(CFG_LIB) $(COMMON_LIB) $(SCSI_LIB) $(ADMIN_LIB) \
                $(MOD_LOAD_LIB)
test_scsi_CFLAGS=-I$(TO_TEST)

test_store_SOURCES=test_store.c
test_store_LDADD=$(STORE_LIB)
test_store_CFLAGS=-I$(TO_TEST)

test_store_retry_SOURCES=test_store_retry.c
test_store_retry_LDADD=$(ADMIN_LIB) $(STORE_LIB)
test_store_retry_CFLAGS=-I$(TO_TEST)

test_undelete_SOURCES=test_undelete.c
test_undelete_LDADD=$(STORE_LIB)
test_undelete_CFLAGS=-I$(TO_TEST)
