AM_CFLAGS=$(CC_OPT) @CMOCKA_CFLAGS@
AM_LDFLAGS=@CMOCKA_LIBS@

check_PROGRAMS=test_locate
noinst_PROGRAMS=$(check_PROGRAMS)

TESTS=test_delete.sh test_undelete.sh test_put.sh test_object_list.sh \
      test_extent_list.sh test_ping.sh test_overwrite.sh test_locate.sh

test_locate_SOURCES=test_locate.c
test_locate_LDADD=../../store/libphobos_store.la ../../dss/libpho_dss.la
