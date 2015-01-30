AM_CFLAGS= $(CC_OPT)

# activated by ./configure --enable-tests
if TESTS_ENABLED

# autotools automatically generate a 'make check' target that:
# 1) build check_PROGRAMS
# 2) run TESTS and generate a summary
check_PROGRAMS=test_common test_attrs test_mapper test_log test_store
TESTS=test_attrs test_mapper test_log test_store.sh

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

endif
