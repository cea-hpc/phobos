AM_CFLAGS= $(CC_OPT)


SWIG_MODULES=phobos/capi/config.py \
	     phobos/capi/_config.so \
	     phobos/capi/config_wrap.c \
	     phobos/capi/dss.py \
	     phobos/capi/_dss.so \
	     phobos/capi/dss_wrap.c
	     phobos/capi/lrs.py \
	     phobos/capi/_lrs.so \
	     phobos/capi/lrs_wrap.c

HAND_MODULES=phobos/capi/clogging.so


all-local:
	swig -python -I../include phobos/capi/config.i
	swig -python -I../include phobos/capi/dss.i
	swig -python -I../include phobos/capi/lrs.i
	python setup.py build_ext --inplace --verbose

clean-local:
	rm -f $(SWIG_MODULES) $(HAND_MODULES)

pre-check-local:
	../tests/test_lock.sh

check-local: all-local pre-check-local
	(cd tests; ./run_tests.sh)
	rm -f /tmp/phobos_test.lock

.PHONY: all-local
