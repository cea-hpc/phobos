AUTOMAKE_OPTIONS=subdir-objects

TEST_EXTENSIONS=.sh
TESTS_ENVIRONMENT=export EXEC_NONREGRESSION=yes;

check_SCRIPTS=acceptance.sh

if RADOS_ENABLED
check_SCRIPTS+=test_rados_rpms.sh
endif

TESTS=$(check_SCRIPTS)
