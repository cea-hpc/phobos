AM_CFLAGS= $(CC_OPT)


SWIG_MODULES=phobos/capi/dss.py \
	     phobos/capi/_dss.so \
	     phobos/capi/dss_wrap.c
	     phobos/capi/lrs.py \
	     phobos/capi/_lrs.so \
	     phobos/capi/lrs_wrap.c \
	     phobos/capi/ldm.py \
	     phobos/capi/_ldm.so \
	     phobos/capi/ldm_wrap.c \
	     phobos/capi/store.py \
	     phobos/capi/_store.so \
	     phobos/capi/store_wrap.c

HAND_MODULES=phobos/capi/const.so

EXTRA_DIST=phobos/capi/dss.i \
	   phobos/capi/lrs.i \
	   phobos/capi/ldm.i \
	   phobos/capi/store.i \
	   phobos/capi/__init__.py \
	   phobos/capi/const_module.c \
	   phobos/__init__.py \
	   phobos/cfg.py \
	   phobos/cli.py \
	   phobos/dss.py \
	   phobos/ffi.py \
	   phobos/log.py \
	   phobos/lrs.py \
	   phobos/output.py \
	   phobos/store.py \
	   phobos/types.py \
	   scripts/phobos \
	   setup.py

all-local:
	swig -python -I../include phobos/capi/dss.i
	swig -python -I../include phobos/capi/lrs.i
	swig -python -I../include phobos/capi/ldm.i
	swig -python -I../include phobos/capi/store.i
	python setup.py build --verbose

clean-local:
	rm -f $(SWIG_MODULES) $(HAND_MODULES)

pre-check-local:
	../tests/test_lock.sh

check-local: all-local pre-check-local
	(cd tests; ./run_tests.sh)
	rm -f /tmp/phobos_test.lock

.PHONY: all-local
