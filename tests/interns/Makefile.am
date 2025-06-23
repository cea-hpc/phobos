AUTOMAKE_OPTIONS=subdir-objects

TO_SRC=../../src

AM_CFLAGS=$(CC_OPT) @CMOCKA_CFLAGS@
AM_LDFLAGS=@CMOCKA_LIBS@

ADMIN_LIB=$(TO_SRC)/admin/libphobos_admin.la
CORE_LIB=$(TO_SRC)/core/libpho_core.la
DAEMON_LIB=$(TO_SRC)/daemon/libpho_daemon.la
MOD_LOAD_LIB=$(TO_SRC)/module-loader/libpho_module_loader.la
IO_LIB=$(TO_SRC)/io/libpho_io.la $(MOD_LOAD_LIB)
LAYOUT_LIB=$(TO_SRC)/layout/libpho_layout.la
RAID1_LIB=$(TO_SRC)/layout-modules/libpho_layout_raid1.la
LDM_LIB=$(TO_SRC)/ldm/libpho_ldm.la $(MOD_LOAD_LIB)
LDM_SCSI_LIB=$(TO_SRC)/ldm-modules/libpho_lib_adapter_scsi.la
SCSI_TAPE_LIB=$(TO_SRC)/ldm-modules/libpho_dev_adapter_scsi_tape.la
FS_POSIX_LIB=$(TO_SRC)/ldm-modules/libpho_fs_adapter_posix.la
FS_LTFS_LIB=$(TO_SRC)/ldm-modules/libpho_fs_adapter_ltfs.la
IO_POSIX_LIB=$(TO_SRC)/io-modules/libpho_io_adapter_posix.la
IO_LTFS_LIB=$(TO_SRC)/io-modules/libpho_io_adapter_ltfs.la
MAPPER_LIB=$(TO_SRC)/io-modules/libpho_mapper.la
LRS_LIB=$(TO_SRC)/lrs/libpho_lrs.la
STORE_LIB=$(TO_SRC)/store/libphobos_store.la
SCSI_LIB=$(TO_SRC)/tlc/scsi/libpho_scsi.la
TLC_LIB=$(TO_SRC)/tlc/libpho_tlc.la

TESTS_LIB_DEPS=$(LRS_LIB) $(LDM_LIB) $(IO_LIB) $(CORE_LIB) $(DAEMON_LIB) \
               $(TLC_LIB)
TESTS_LIB=../libpho_test_utils.la
TESTS_LIB_INCLUDES=-I$(TO_SRC)/tlc -I$(TO_SRC)/tlc/scsi -I..

if VALGRIND_ENABLED
@VALGRIND_CHECK_RULES@
VALGRIND_SUPPRESSIONS_FILES=../supp-files/valgrind.supp ../supp-files/glib.supp
VALGRIND_FLAGS=-q --leak-check=full --errors-for-leak-kinds=definite --show-possibly-lost=no
endif

TEST_EXTENSIONS=.sh

check_PROGRAMS=test_attrs \
               test_cfg \
               test_common \
               test_communication \
               test_copy_cfg \
               test_dev_tape \
               test_dss_copy \
               test_dss_extent \
               test_dss_find_object \
               test_dss_lazy_find_object \
               test_dss_lock \
               test_dss_logs \
               test_dss_medium_locate \
               test_dss_object_move \
               test_io \
               test_layout_module \
               test_ldm \
               test_log \
               test_lrs_cfg \
               test_lrs_device \
               test_lrs_scheduling \
               test_ltfs_logs \
               test_mapper \
               test_phobos_admin_medium_locate \
               test_pho_cache \
               test_ping \
               test_scsi_logs \
               test_store_profile \
               test_store_object_md \
               test_store_object_md_get \
               test_type_utils

TESTS=$(check_PROGRAMS)

test_attrs_SOURCES=test_attrs.c
test_attrs_LDADD=$(TESTS_LIB) $(TESTS_LIB_DEPS)
test_attrs_CFLAGS=$(AM_CFLAGS) -I..

test_cfg_SOURCES=test_cfg.c
test_cfg_LDADD=$(TESTS_LIB) $(TESTS_LIB_DEPS)
test_cfg_CFLAGS=$(AM_CFLAGS) -I..

test_common_SOURCES=test_common.c
test_common_LDADD=$(TESTS_LIB) $(TESTS_LIB_DEPS)
test_common_CFLAGS=$(AM_CFLAGS) -I..

test_communication_SOURCES=test_communication.c
test_communication_LDADD=$(CORE_LIB) $(TESTS_LIB) $(TESTS_LIB_DEPS)
test_communication_CFLAGS=$(AM_CFLAGS) -I..

test_copy_cfg_SOURCES=test_copy_cfg.c
test_copy_cfg_LDADD=$(CORE_LIB)
test_copy_cfg_CFLAGS=$(AM_CFLAGS) -I..

test_dev_tape_SOURCES=test_dev_tape.c
test_dev_tape_LDADD=$(SCSI_TAPE_LIB) $(TESTS_LIB) $(TESTS_LIB_DEPS)
test_dev_tape_CFLAGS=$(AM_CFLAGS) -I..

test_dss_extent_SOURCES=test_dss_extent.c
test_dss_extent_LDADD=$(ADMIN_LIB) $(TESTS_LIB) $(TESTS_LIB_DEPS)
test_dss_extent_CFLAGS=$(AM_CFLAGS) $(TESTS_LIB_INCLUDES)

test_dss_copy_SOURCES=test_dss_copy.c
test_dss_copy_LDADD=$(ADMIN_LIB) $(TESTS_LIB) $(TESTS_LIB_DEPS)
test_dss_copy_CFLAGS=$(AM_CFLAGS) $(TESTS_LIB_INCLUDES)

test_dss_find_object_SOURCES=test_dss_find_object.c
test_dss_find_object_LDADD=$(ADMIN_LIB) $(TESTS_LIB) $(TESTS_LIB_DEPS)
test_dss_find_object_CFLAGS=$(AM_CFLAGS) -I$(TO_SRC)/store \
                            $(TESTS_LIB_INCLUDES)

test_dss_lazy_find_object_SOURCES=test_dss_lazy_find_object.c
test_dss_lazy_find_object_LDADD=$(ADMIN_LIB) $(TESTS_LIB) $(TESTS_LIB_DEPS)
test_dss_lazy_find_object_CFLAGS=$(AM_CFLAGS) -I$(TO_SRC)/store \
                                 $(TESTS_LIB_INCLUDES)

test_dss_lock_SOURCES=test_dss_lock.c
test_dss_lock_LDADD=$(ADMIN_LIB) $(TESTS_LIB) $(TESTS_LIB_DEPS)
test_dss_lock_CFLAGS=$(AM_CFLAGS) -I$(TO_SRC)/core/dss $(TESTS_LIB_INCLUDES)

test_dss_logs_SOURCES=test_dss_logs.c
test_dss_logs_LDADD=$(ADMIN_LIB) $(TESTS_LIB) $(TESTS_LIB_DEPS)
test_dss_logs_CFLAGS=$(AM_CFLAGS) -I$(TO_SRC)/core/dss $(TESTS_LIB_INCLUDES)

test_dss_medium_locate_SOURCES=test_dss_medium_locate.c
test_dss_medium_locate_LDADD=$(ADMIN_LIB) $(TESTS_LIB) $(TESTS_LIB_DEPS)
test_dss_medium_locate_CFLAGS=$(AM_CFLAGS) -I$(TO_SRC)/core/dss \
        $(TESTS_LIB_INCLUDES)

test_dss_object_move_SOURCES=test_dss_object_move.c
test_dss_object_move_LDADD=$(ADMIN_LIB) $(TESTS_LIB) $(TESTS_LIB_DEPS)
test_dss_object_move_CFLAGS=$(AM_CFLAGS) $(TESTS_LIB_INCLUDES)

test_io_SOURCES=test_io.c
test_io_LDADD=$(IO_POSIX_LIB) $(TESTS_LIB) $(TESTS_LIB_DEPS)
test_io_CFLAGS=$(AM_CFLAGS) -I$(TO_SRC)/io-modules -I..
if RADOS_ENABLED
test_io_LDADD+=$(ADMIN_LIB)
endif

test_layout_module_SOURCES=test_layout_module.c
test_layout_module_LDADD=$(LAYOUT_LIB) $(IO_POSIX_LIB) $(RAID1_LIB) \
                         $(CORE_LIB) -ldl
test_layout_module_CFLAGS=$(AM_CFLAGS) -I..

test_ldm_SOURCES=test_ldm.c
test_ldm_LDADD=$(FS_POSIX_LIB) $(TESTS_LIB) $(TESTS_LIB_DEPS)
test_ldm_CFLAGS=$(AM_CFLAGS) -I$(TO_SRC)/ldm-modules -I..

test_log_SOURCES=test_log.c
test_log_LDADD=$(TESTS_LIB) $(TESTS_LIB_DEPS)
test_log_CFLAGS=$(AM_CFLAGS) -I..

test_lrs_cfg_SOURCES=test_lrs_cfg.c
test_lrs_cfg_LDADD=$(LRS_LIB) $(CORE_LIB)
test_lrs_cfg_CFLAGS=$(AM_CFLAGS) -I$(TO_SRC)/lrs

test_lrs_device_SOURCES=test_lrs_device.c
test_lrs_device_LDADD=$(ADMIN_LIB) $(TESTS_LIB) $(TESTS_LIB_DEPS)
test_lrs_device_CFLAGS=$(AM_CFLAGS) -I$(TO_SRC)/lrs $(TESTS_LIB_INCLUDES)

test_lrs_scheduling_SOURCES=test_lrs_scheduling.c
test_lrs_scheduling_LDADD=$(LRS_LIB) $(LDM_LIB) $(MOD_LOAD_LIB) $(CORE_LIB) \
                          $(CFG_LIB) $(IO_LIB) $(TESTS_LIB) $(TESTS_LIB_DEPS)
test_lrs_scheduling_CFLAGS=$(AM_CFLAGS) -I$(TO_SRC)/lrs -I..

test_ltfs_logs_SOURCES=test_ltfs_logs.c
test_ltfs_logs_LDADD=$(MOD_LOAD_LIB) $(SCSI_LIB) $(LDM_SCSI_LIB) \
                     $(IO_LTFS_LIB) $(FS_LTFS_LIB) $(ADMIN_LIB) $(TESTS_LIB) \
                     $(TESTS_LIB_DEPS) $(TLC_LIB)
test_ltfs_logs_CFLAGS=$(AM_CFLAGS) -I$(TO_SRC)/lrs -I$(TO_SRC)/ldm-modules \
                      -I..  $(TESTS_LIB_INCLUDES)

test_mapper_SOURCES=test_mapper.c
test_mapper_LDADD=$(STORE_LIB) $(MAPPER_LIB) $(TESTS_LIB) $(TESTS_LIB_DEPS)
test_mapper_CFLAGS=$(AM_CFLAGS) -I..

test_phobos_admin_medium_locate_SOURCES=test_phobos_admin_medium_locate.c
test_phobos_admin_medium_locate_LDADD=$(ADMIN_LIB) $(TESTS_LIB) \
                                      $(TESTS_LIB_DEPS)
test_phobos_admin_medium_locate_CFLAGS=$(AM_CFLAGS) -I$(TO_SRC)/core/dss \
                                       $(TESTS_LIB_INCLUDES)

test_pho_cache_SOURCES=test_pho_cache.c
test_pho_cache_LDADD=$(ADMIN_LIB) $(TESTS_LIB) $(TESTS_LIB_DEPS)
test_pho_cache_CFLAGS=$(AM_CFLAGS) -I$(TO_SRC)/core/dss -I..

test_ping_SOURCES=test_ping.c mock_communication.c
test_ping_LDADD=$(ADMIN_LIB) $(CORE_LIB) $(LDM_LIB)
test_ping_CFLAGS=$(AM_CFLAGS) -I$(TO_SRC)/admin

test_scsi_logs_SOURCES=test_scsi_logs.c
test_scsi_logs_LDADD=$(MOD_LOAD_LIB) $(SCSI_LIB) $(LDM_SCSI_LIB) $(ADMIN_LIB) \
                     $(TESTS_LIB) $(TESTS_LIB_DEPS) $(TLC_LIB)
test_scsi_logs_CFLAGS=$(AM_CFLAGS) -I$(TO_SRC)/lrs -I$(TO_SRC)/ldm-modules \
                      $(TESTS_LIB_INCLUDES)

# TODO: try to link against the phobos_store library instead of
# the store_profile object file
test_store_profile_SOURCES=test_store_profile.c
test_store_profile_LDADD=$(CORE_LIB) $(TO_SRC)/store/.libs/store_profile.o
test_store_profile_CFLAGS=$(AM_CFLAGS) -I$(TO_SRC)/store

test_store_object_md_SOURCES=test_store_object_md.c
test_store_object_md_LDADD=$(STORE_LIB)
test_store_object_md_CFLAGS=$(AM_CFLAGS) -I$(TO_SRC)/core/dss -I$(TO_SRC)/store

test_store_object_md_get_SOURCES=test_store_object_md_get.c
test_store_object_md_get_LDADD=$(STORE_LIB) $(ADMIN_LIB) $(TESTS_LIB) \
                               $(TESTS_LIB_DEPS)
test_store_object_md_get_CFLAGS=$(AM_CFLAGS) -I$(TO_SRC)/store \
                                $(TESTS_LIB_INCLUDES)

test_type_utils_SOURCES=test_type_utils.c
test_type_utils_LDADD=$(TESTS_LIB) $(TESTS_LIB_DEPS)
test_type_utils_CFLAGS=$(AM_CFLAGS) -I..
