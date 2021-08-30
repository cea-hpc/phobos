TO_SRC=../src

AM_CFLAGS= $(CC_OPT) @CMOCKA_CFLAGS@
AM_LDFLAGS=@CMOCKA_LIBS@

CFG_LIB=$(TO_SRC)/cfg/libpho_cfg.la
COMMON_LIB=$(TO_SRC)/common/libpho_common.la
DSS_LIB=$(TO_SRC)/dss/libpho_dss.la
RAID1_LIB=$(TO_SRC)/layout-modules/libpho_layout_raid1.la

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

# List of binary files to compile
check_PROGRAMS=test_raid1_split_locate

# List of scripts which will be directly tested by make check
tests_sh=acceptance.sh test_raid1_split_locate.sh test_daemon.sh

# List of scripts
check_SCRIPTS=$(tests_sh) setup_db.sh test_env.sh test_launch_daemon.sh

TESTS=$(tests_sh)

pre-check-local:
	./test_lock.sh

check-local: pre-check-local
	rm -f /tmp/phobos_test.lock

test_raid1_split_locate_SOURCES=test_raid1_split_locate.c
test_raid1_split_locate_LDADD=$(CFG_LIB) $(COMMON_LIB) $(DSS_LIB) $(RAID1_LIB)
