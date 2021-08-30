# FIXME:
# Possibly-lost leaks appear in valgrind reports when testing
# externs/cli/acceptance.sh, generating millions of lines (each phobos command
# execution leads to ~5k leaks). Those leaks are somewhat bound to the python
# CLI, and are not clear enough to be resolve for now.
#
# I made some research and there is a way to get better reports and then try to
# resolve those leaks:
# - get an environment where python is compiled in debug mode to let valgrind
#   catch more information
#   $ ./configure --with-pydebug --without-pymalloc --with-valgrind

pre-check-local:
	./test_lock.sh

check-local: pre-check-local
	rm -f /tmp/phobos_test.lock
