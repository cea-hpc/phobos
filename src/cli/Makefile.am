AM_CFLAGS= $(CC_OPT)

C_MODULES_SO=phobos/core/const.so \
	     phobos/core/glue.so

EXTRA_DIST=phobos/__init__.py \
	   phobos/cli.py \
	   phobos/output.py \
	   phobos/core/__init__.py \
	   phobos/core/cfg.py \
	   phobos/core/const_module.c \
	   phobos/core/dss.py \
	   phobos/core/ffi.py \
	   phobos/core/glue_module.c \
	   phobos/core/ldm.py \
	   phobos/core/log.py \
	   phobos/core/lrs.py \
	   phobos/core/store.py \
	   phobos/db/__init__.py \
	   phobos/db/__main__.py \
	   phobos/db/db_config.py \
	   scripts/phobos \
	   setup.py

all-local:
	python setup.py build --verbose

clean-local:
	rm -f $(C_MODULES_SO)

pre-check-local:
	../tests/test_lock.sh

check-local: all-local pre-check-local
	(cd tests; ./run_tests.sh)
	rm -f /tmp/phobos_test.lock

.PHONY: all-local
