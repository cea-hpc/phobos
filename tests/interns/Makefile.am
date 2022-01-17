AUTOMAKE_OPTIONS=subdir-objects

TO_SRC=../../src

AM_CFLAGS=$(CC_OPT) @CMOCKA_CFLAGS@
AM_LDFLAGS=@CMOCKA_LIBS@

ADMIN_LIB=$(TO_SRC)/admin/libphobos_admin.la
CFG_LIB=$(TO_SRC)/cfg/libpho_cfg.la
COMMON_LIB=$(TO_SRC)/common/libpho_common.la
COMMUNICATION_LIB=$(TO_SRC)/communication/libpho_comm.la
DSS_LIB=$(TO_SRC)/dss/libpho_dss.la
IO_LIB=$(TO_SRC)/io/libpho_io.la
LAYOUT_LIB=$(TO_SRC)/layout/libpho_layout.la
RAID1_LIB=$(TO_SRC)/layout-modules/libpho_layout_raid1.la
LDM_LIB=$(TO_SRC)/ldm/libpho_ldm.la
LRS_LIB=$(TO_SRC)/lrs/libpho_lrs.la
STORE_LIB=$(TO_SRC)/store/libphobos_store.la

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
               test_dev_tape \
               test_dss_lazy_find_object \
               test_dss_lock \
               test_dss_medium_locate \
               test_dss_object_move \
               test_io \
               test_layout_module \
               test_ldm \
               test_log \
               test_lrs_cfg \
               test_mapper \
               test_phobos_admin_medium_locate \
               test_ping \
               test_store_alias \
               test_store_object_md \
               test_store_object_md_get \
               test_type_utils

TESTS=$(check_PROGRAMS)

test_attrs_SOURCES=test_attrs.c
test_attrs_LDADD=$(COMMON_LIB)
test_attrs_CFLAGS=$(AM_CFLAGS) -I..

test_cfg_SOURCES=test_cfg.c
test_cfg_LDADD=$(CFG_LIB) $(COMMON_LIB)
test_cfg_CFLAGS=$(AM_CFLAGS) -I..

test_common_SOURCES=test_common.c
test_common_LDADD=$(COMMON_LIB)
test_common_CFLAGS=$(AM_CFLAGS) -I..

test_communication_SOURCES=test_communication.c
test_communication_LDADD=$(COMMON_LIB) $(COMMUNICATION_LIB)
test_communication_CFLAGS=$(AM_CFLAGS) -I..

test_dev_tape_SOURCES=test_dev_tape.c
test_dev_tape_LDADD=$(LDM_LIB) $(CFG_LIB) $(COMMON_LIB)
test_dev_tape_CFLAGS=$(AM_CFLAGS) -I..

test_dss_lazy_find_object_SOURCES=test_dss_lazy_find_object.c ../test_setup.c \
                                  ../test_setup.h
test_dss_lazy_find_object_LDADD=$(DSS_LIB) $(CFG_LIB) $(COMMON_LIB) \
                                $(ADMIN_LIB) $(LDM_LIB)
test_dss_lazy_find_object_CFLAGS=$(AM_CFLAGS) -I$(TO_SRC)/store

test_dss_lock_SOURCES=test_dss_lock.c ../test_setup.c ../test_setup.h
test_dss_lock_LDADD=$(DSS_LIB) $(ADMIN_LIB) $(CFG_LIB) $(COMMON_LIB) $(LDM_LIB)
test_dss_lock_CFLAGS=$(AM_CFLAGS) -I$(TO_SRC)/dss

test_dss_medium_locate_SOURCES=test_dss_medium_locate.c ../test_setup.c \
                               ../test_setup.h
test_dss_medium_locate_LDADD=$(DSS_LIB) $(CFG_LIB) $(COMMON_LIB) $(ADMIN_LIB) \
                             $(LDM_LIB)
test_dss_medium_locate_CFLAGS=$(AM_CFLAGS) -I$(TO_SRC)/dss

test_dss_object_move_SOURCES=test_dss_object_move.c ../test_setup.c \
                             ../test_setup.h
test_dss_object_move_LDADD=$(DSS_LIB) $(ADMIN_LIB) $(CFG_LIB) $(COMMON_LIB) \
                           $(LDM_LIB)

test_io_SOURCES=test_io.c
test_io_LDADD=$(IO_LIB) $(COMMON_LIB)
test_io_CFLAGS=$(AM_CFLAGS) -I..

test_layout_module_SOURCES=test_layout_module.c
test_layout_module_LDADD=$(LAYOUT_LIB) $(RAID1_LIB) $(CFG_LIB) $(COMMON_LIB) -ldl
test_layout_module_CFLAGS=$(AM_CFLAGS) -I..

test_ldm_SOURCES=test_ldm.c
test_ldm_LDADD=$(LDM_LIB) $(CFG_LIB) $(COMMON_LIB)
test_ldm_CFLAGS=$(AM_CFLAGS) -I$(TO_SRC)/ldm -I..

test_log_SOURCES=test_log.c
test_log_LDADD=$(COMMON_LIB)
test_log_CFLAGS=$(AM_CFLAGS) -I..

test_lrs_cfg_SOURCES=test_lrs_cfg.c
test_lrs_cfg_LDADD=$(LRS_LIB) $(CFG_LIB) $(COMMON_LIB)
test_lrs_cfg_CFLAGS=$(AM_CFLAGS) -I$(TO_SRC)/lrs

test_mapper_SOURCES=test_mapper.c
test_mapper_LDADD=$(STORE_LIB)
test_mapper_CFLAGS=$(AM_CFLAGS) -I..

test_phobos_admin_medium_locate_SOURCES=test_phobos_admin_medium_locate.c \
                                        ../test_setup.c ../test_setup.h
test_phobos_admin_medium_locate_LDADD=$(ADMIN_LIB) $(DSS_LIB) $(CFG_LIB) \
                                      $(COMMON_LIB) $(LDM_LIB)
test_phobos_admin_medium_locate_CFLAGS=$(AM_CFLAGS) -I$(TO_SRC)/dss

test_ping_SOURCES=test_ping.c mock_communication.c
test_ping_LDADD=$(ADMIN_LIB) $(COMMON_LIB) $(DSS_LIB) $(LDM_LIB)
test_ping_CFLAGS=$(AM_CFLAGS) -I$(TO_SRC)/admin

# TODO: try to link against the phobos_store library instead of
# the store_alias object file
test_store_alias_SOURCES=test_store_alias.c
test_store_alias_LDADD=$(CFG_LIB) $(COMMON_LIB) \
                       $(TO_SRC)/store/.libs/store_alias.o
test_store_alias_CFLAGS=$(AM_CFLAGS) -I$(TO_SRC)/store

test_store_object_md_SOURCES=test_store_object_md.c
test_store_object_md_LDADD=$(STORE_LIB)
test_store_object_md_CFLAGS=$(AM_CFLAGS) -I$(TO_SRC)/dss -I$(TO_SRC)/store

test_store_object_md_get_SOURCES=test_store_object_md_get.c ../test_setup.c
test_store_object_md_get_LDADD=$(STORE_LIB) $(ADMIN_LIB)
test_store_object_md_get_CFLAGS=$(AM_CFLAGS) -I$(TO_SRC)/store

test_type_utils_SOURCES=test_type_utils.c
test_type_utils_LDADD=$(COMMON_LIB)
test_type_utils_CFLAGS=$(AM_CFLAGS) -I..
