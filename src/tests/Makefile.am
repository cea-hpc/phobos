AM_CFLAGS= $(CC_OPT)

check_PROGRAMS=test_common test_attrs test_mapper


test_common_SOURCES=../common.c test_common.c
test_attrs_SOURCES=../attrs.c test_attrs.c

test_mapper_SOURCES=../mapper.c test_mapper.c
test_mapper_LDADD=-lcrypto

test_attrs_LDADD=-ljansson
