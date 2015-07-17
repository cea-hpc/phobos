AM_CFLAGS= $(CC_OPT)

# activated by ./configure --enable-tests
if TESTS_ENABLED

# autotools automatically generate a 'make check' target that:
# 1) build check_PROGRAMS
# 2) run TESTS and generate a summary
check_PROGRAMS=test_common test_attrs test_mapper test_log test_store test_dss test_cfg
TESTS=test_attrs test_mapper test_log test_cfg test_dss.sh test_store.sh

noinst_PROGRAMS=$(check_PROGRAMS)

test_log_SOURCES=test_log.c
test_log_LDADD=../common/libpho_common.la

test_common_SOURCES=test_common.c
test_common_LDADD=../common/libpho_common.la

test_attrs_SOURCES=test_attrs.c
test_attrs_LDADD=../common/libpho_common.la

test_mapper_SOURCES=test_mapper.c
test_mapper_LDADD=../store/libphobos_store.la

test_store_SOURCES=test_store.c
test_store_LDADD=../store/libphobos_store.la

test_dss_SOURCES=test_dss.c
test_dss_LDADD=../dss/libpho_dss.la ../common/libpho_common.la

test_cfg_SOURCES=test_cfg.c
test_cfg_LDADD=../cfg/libpho_cfg.la ../common/libpho_common.la
endif
