AM_CFLAGS= $(CC_OPT) @CMOCKA_CFLAGS@
AM_LDFLAGS=@CMOCKA_LIBS@

# autotools automatically generate a 'make check' target that:
# 1) build check_PROGRAMS
# 2) run TESTS and generate a summary
check_PROGRAMS=test_common test_attrs test_mapper test_log test_store	\
	       test_dss test_cfg test_scsi test_dev_tape \
	       test_ldm test_type_utils test_store_retry test_io \
	       test_communication test_bad_comm test_store_alias test_delete \
	       test_undelete test_raid1_split_locate

# 1st step: low level tests
UNIT_TESTS=test_common test_attrs test_mapper test_log test_cfg test_dev_tape \
	   test_ldm test_type_utils test_io test_communication test_store_alias

# 2nd step: integration tests
INTEG_TESTS=test_dss.sh test_scsi.sh test_layouts.sh acceptance.sh	\
	    test_store_retry.sh simple_split.sh raid1_simple_split.sh 	\
	    raid1_split.sh test_bad_comm.sh test_resource_availability.sh \
	    test_daemon.sh test_media_operation_flags.sh test_tag_update.sh \
	    test_delete.sh test_undelete.sh

TESTS=$(UNIT_TESTS) $(INTEG_TESTS)

noinst_PROGRAMS=$(check_PROGRAMS)
noinst_HEADERS=pho_test_utils.h pho_test_xfer_utils.h

SUBDIRS=. unit integration

pre-check-local:
	./test_lock.sh

check-local: pre-check-local
	rm -f /tmp/phobos_test.lock

test_log_SOURCES=test_log.c
test_log_LDADD=../common/libpho_common.la

test_bad_comm_SOURCES=test_bad_comm.c
test_bad_comm_LDADD=../communication/libpho_comm.la ../common/libpho_common.la \
		    ../serializer/libpho_serializer.la

test_common_SOURCES=test_common.c
test_common_LDADD=../common/libpho_common.la

test_communication_SOURCES=test_communication.c
test_communication_LDADD=../communication/libpho_comm.la ../common/libpho_common.la

test_attrs_SOURCES=test_attrs.c
test_attrs_LDADD=../common/libpho_common.la

test_mapper_SOURCES=test_mapper.c
test_mapper_LDADD=../store/libphobos_store.la

test_store_SOURCES=test_store.c
test_store_LDADD=../store/libphobos_store.la

test_dss_SOURCES=test_dss.c
test_dss_LDADD=../dss/libpho_dss.la ../cfg/libpho_cfg.la ../common/libpho_common.la

test_cfg_SOURCES=test_cfg.c
test_cfg_LDADD=../cfg/libpho_cfg.la ../common/libpho_common.la

test_scsi_SOURCES=test_scsi.c
test_scsi_LDADD=../ldm/libpho_ldm.la ../cfg/libpho_cfg.la ../common/libpho_common.la

test_dev_tape_SOURCES=test_dev_tape.c
test_dev_tape_LDADD=../ldm/libpho_ldm.la ../cfg/libpho_cfg.la ../common/libpho_common.la

test_ldm_SOURCES=test_ldm.c
test_ldm_LDADD=../ldm/libpho_ldm.la ../cfg/libpho_cfg.la ../common/libpho_common.la

test_type_utils_SOURCES=test_type_utils.c
test_type_utils_LDADD=../common/libpho_common.la

test_store_retry_SOURCES=test_store_retry.c
test_store_retry_LDADD=../store/libphobos_store.la ../admin/libphobos_admin.la

test_io_SOURCES=test_io.c
test_io_LDADD=../io/libpho_io.la ../common/libpho_common.la

test_store_alias_SOURCES=test_store_alias.c
test_store_alias_LDADD=../cfg/libpho_cfg.la ../common/libpho_common.la \
			../store/.libs/store_alias.o

test_delete_SOURCES=test_delete.c
test_delete_LDADD=../store/libphobos_store.la

test_undelete_SOURCES=test_undelete.c
test_undelete_LDADD=../store/libphobos_store.la

test_raid1_split_locate_SOURCES=test_raid1_split_locate.c
test_raid1_split_locate_LDADD=../cfg/libpho_cfg.la ../common/libpho_common.la \
                              ../dss/libpho_dss.la \
                              ../layout-modules/libpho_layout_raid1.la

@VALGRIND_CHECK_RULES@
VALGRIND_SUPPRESSIONS_FILES=supp-files/valgrind.supp supp-files/glib.supp
EXTRA_DIST=setup_db.sh test_env.sh test_launch_daemon.sh \
           $(VALGRIND_SUPPRESSIONS_FILES)
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
VALGRIND_memcheck_FLAGS=-q --leak-check=full --errors-for-leak-kinds=definite --show-possibly-lost=no

AUTOMAKE_OPTIONS=parallel-tests
TEST_EXTENSIONS=.sh
LOG_COMPILER=$(VALGRIND)
