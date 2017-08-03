AM_CFLAGS= $(CC_OPT)

C_MODULES_SO=phobos/capi/const.so

EXTRA_DIST=phobos/capi/__init__.py \
	   phobos/capi/const_module.c \
	   phobos/__init__.py \
	   phobos/cfg.py \
	   phobos/cli.py \
	   phobos/dss.py \
	   phobos/ffi.py \
	   phobos/ldm.py \
	   phobos/log.py \
	   phobos/lrs.py \
	   phobos/output.py \
	   phobos/store.py \
	   phobos/types.py \
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
