TO_SRC=../src

AM_CFLAGS= $(CC_OPT) @CMOCKA_CFLAGS@
AM_LDFLAGS=@CMOCKA_LIBS@

ADMIN_LIB=$(TO_SRC)/admin/libphobos_admin.la
CFG_LIB=$(TO_SRC)/cfg/libpho_cfg.la
COMMON_LIB=$(TO_SRC)/common/libpho_common.la
COMMUNICATION_LIB=$(TO_SRC)/communication/libpho_comm.la
DSS_LIB=$(TO_SRC)/dss/libpho_dss.la
IO_LIB=$(TO_SRC)/io/libpho_io.la
LDM_LIB=$(TO_SRC)/ldm/libpho_ldm.la
RAID1_LIB=$(TO_SRC)/layout-modules/libpho_layout_raid1.la
SERIALIZER_LIB=$(TO_SRC)/serializer/libpho_serializer.la
STORE_LIB=$(TO_SRC)/store/libphobos_store.la

# FIXME:
# Possibly-lost leaks appear in valgrind reports when testing acceptance.sh,
# generating millions of lines (each phobos command execution leads to ~5k
# leaks). Those leaks are somewhat bound to the python CLI, and are not clear
# enough to be resolve for now.
#
# I made some research and there is a way to get better reports and then try to
# resolve those leaks:
# - get an environment where python is compiled in debug mode to let valgrind
#   catch more information
#   $ ./configure --with-pydebug --without-pymalloc --with-valgrind

if VALGRIND_ENABLED
@VALGRIND_CHECK_RULES@
VALGRIND_SUPPRESSIONS_FILES=supp-files/valgrind.supp supp-files/glib.supp
VALGRIND_FLAGS=-q --leak-check=full --errors-for-leak-kinds=definite --show-possibly-lost=no
endif

TEST_EXTENSIONS=.sh

# autotools automatically generates a 'make check' target that:
# 1) build check_PROGRAMS
# 2) run TESTS and generate a summary

# List of binary files which will be directly tested by make check
tests_bin=test_common test_attrs test_mapper test_log test_cfg test_dev_tape \
	      test_ldm test_type_utils test_io test_communication test_store_alias

# List of binary files to compile
check_PROGRAMS=$(tests_bin) test_store test_dss test_scsi test_store_retry \
	           test_bad_comm test_delete test_undelete test_raid1_split_locate

# List of scripts which will be directly tested by make check
tests_sh=test_dss.sh test_scsi.sh test_layouts.sh acceptance.sh	\
	     test_store_retry.sh simple_split.sh raid1_simple_split.sh 	\
	     raid1_split.sh test_bad_comm.sh test_resource_availability.sh \
	     test_daemon.sh test_media_operation_flags.sh test_tag_update.sh \
	     test_delete.sh test_undelete.sh

# List of scripts
check_SCRIPTS=$(tests_sh) setup_db.sh test_env.sh test_launch_daemon.sh

TESTS=$(tests_bin) $(tests_sh)

pre-check-local:
	./test_lock.sh

check-local: pre-check-local
	rm -f /tmp/phobos_test.lock

test_log_SOURCES=test_log.c pho_test_utils.h
test_log_LDADD=$(COMMON_LIB)

test_bad_comm_SOURCES=test_bad_comm.c pho_test_utils.h
test_bad_comm_LDADD=$(COMMON_LIB) $(COMMUNICATION_LIB) $(SERIALIZER_LIB)

test_common_SOURCES=test_common.c pho_test_utils.h
test_common_LDADD=$(COMMON_LIB)

test_communication_SOURCES=test_communication.c pho_test_utils.h
test_communication_LDADD=$(COMMON_LIB) $(COMMUNICATION_LIB)

test_attrs_SOURCES=test_attrs.c pho_test_utils.h
test_attrs_LDADD=$(COMMON_LIB)

test_mapper_SOURCES=test_mapper.c pho_test_utils.h
test_mapper_LDADD=$(STORE_LIB)

test_store_SOURCES=test_store.c pho_test_utils.h pho_test_xfer_utils.h
test_store_LDADD=$(STORE_LIB)

test_dss_SOURCES=test_dss.c pho_test_utils.h
test_dss_LDADD=$(DSS_LIB) $(CFG_LIB) $(COMMON_LIB)

test_cfg_SOURCES=test_cfg.c pho_test_utils.h
test_cfg_LDADD=$(CFG_LIB) $(COMMON_LIB)

test_scsi_SOURCES=test_scsi.c pho_test_utils.h
test_scsi_LDADD=$(LDM_LIB) $(CFG_LIB) $(COMMON_LIB)

test_dev_tape_SOURCES=test_dev_tape.c pho_test_utils.h
test_dev_tape_LDADD=$(LDM_LIB) $(CFG_LIB) $(COMMON_LIB)

test_ldm_SOURCES=test_ldm.c pho_test_utils.h
test_ldm_LDADD=$(LDM_LIB) $(CFG_LIB) $(COMMON_LIB)

test_type_utils_SOURCES=test_type_utils.c pho_test_utils.h
test_type_utils_LDADD=$(COMMON_LIB)

test_store_retry_SOURCES=test_store_retry.c pho_test_utils.h pho_test_xfer_utils.h
test_store_retry_LDADD=$(ADMIN_LIB) $(STORE_LIB)

test_io_SOURCES=test_io.c pho_test_utils.h
test_io_LDADD=$(IO_LIB) $(COMMON_LIB)

# TODO: try to link against the phobos_store library instead of
# the store_alias object file
test_store_alias_SOURCES=test_store_alias.c
test_store_alias_LDADD=$(CFG_LIB) $(COMMON_LIB) \
                       $(TO_SRC)/store/.libs/store_alias.o

test_delete_SOURCES=test_delete.c pho_test_utils.h
test_delete_LDADD=$(STORE_LIB)

test_undelete_SOURCES=test_undelete.c pho_test_utils.h
test_undelete_LDADD=$(STORE_LIB)

test_raid1_split_locate_SOURCES=test_raid1_split_locate.c
test_raid1_split_locate_LDADD=$(CFG_LIB) $(COMMON_LIB) $(DSS_LIB) $(RAID1_LIB)
