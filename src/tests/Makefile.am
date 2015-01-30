AM_CFLAGS= $(CC_OPT)

# autotools automatically generate a 'make check' target that:
# 1) build check_PROGRAMS
# 2) run TESTS and generate a summary
check_PROGRAMS=test_common test_attrs test_mapper test_log
TESTS=test_attrs test_mapper test_log

test_log_SOURCES=../log.c test_log.c
test_common_SOURCES=../common.c ../log.c test_common.c
test_attrs_SOURCES=../attrs.c ../log.c test_attrs.c
test_mapper_SOURCES=../mapper.c ../log.c test_mapper.c

test_mapper_LDADD=-lcrypto
test_attrs_LDADD=-ljansson
