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
RAID1_LIB=$(TO_SRC)/layout-modules/libpho_layout_raid1.la
SERIALIZER_LIB=$(TO_SRC)/serializer/libpho_serializer.la
STORE_LIB=$(TO_SRC)/store/libphobos_store.la

if VALGRIND_ENABLED
@VALGRIND_CHECK_RULES@
VALGRIND_SUPPRESSIONS_FILES=$(TO_TEST)/supp-files/valgrind.supp $(TO_TEST)/supp-files/glib.supp
VALGRIND_FLAGS=-q --leak-check=full --errors-for-leak-kinds=definite --show-possibly-lost=no
endif

TEST_EXTENSIONS=.sh

check_PROGRAMS=test_locate \
	           test_scsi \
	           test_dss \
	           test_undelete \
	           test_delete \
	           test_bad_comm \
	           test_store_retry \
	           test_store \
	           test_raid1_split_locate

check_SCRIPTS=test_locate.sh \
	          test_scsi.sh \
	          test_dss.sh \
	          test_undelete.sh \
	          test_delete.sh \
	          test_bad_comm.sh \
	          test_store_retry.sh \
	          test_layouts.sh \
	          test_raid1_split_locate.sh

TESTS=$(check_SCRIPTS)

test_locate_SOURCES=test_locate.c ../../test_setup.c
test_locate_LDADD=$(STORE_LIB) $(ADMIN_LIB) $(DSS_LIB) $(LAYOUT_LIB)
test_locate_CFLAGS=$(AM_CFLAGS) -I$(TO_SRC)/dss -I$(TO_TEST)

test_scsi_SOURCES=test_scsi.c
test_scsi_LDADD=$(LDM_LIB) $(CFG_LIB) $(COMMON_LIB)
test_scsi_CFLAGS=-I$(TO_TEST)

test_dss_SOURCES=test_dss.c
test_dss_LDADD=$(DSS_LIB) $(CFG_LIB) $(COMMON_LIB)
test_dss_CFLAGS=-I$(TO_TEST)

test_undelete_SOURCES=test_undelete.c
test_undelete_LDADD=$(STORE_LIB)
test_undelete_CFLAGS=-I$(TO_TEST)

test_delete_SOURCES=test_delete.c
test_delete_LDADD=$(STORE_LIB)
test_delete_CFLAGS=-I$(TO_TEST)

test_bad_comm_SOURCES=test_bad_comm.c
test_bad_comm_LDADD=$(COMMON_LIB) $(COMMUNICATION_LIB) $(SERIALIZER_LIB)
test_bad_comm_CFLAGS=-I$(TO_TEST)

test_store_retry_SOURCES=test_store_retry.c
test_store_retry_LDADD=$(ADMIN_LIB) $(STORE_LIB)
test_store_retry_CFLAGS=-I$(TO_TEST)

test_store_SOURCES=test_store.c
test_store_LDADD=$(STORE_LIB)
test_store_CFLAGS=-I$(TO_TEST)

test_raid1_split_locate_SOURCES=test_raid1_split_locate.c
test_raid1_split_locate_LDADD=$(CFG_LIB) $(COMMON_LIB) $(DSS_LIB) $(RAID1_LIB)
test_raid1_split_locate_CFLAGS=-I$(TO_TEST)
