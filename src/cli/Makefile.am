AM_CFLAGS= $(CC_OPT)

C_MODULES_SO=phobos/core/const.so \
	     phobos/core/glue.so

EXTRA_DIST=phobos/__init__.py \
	   phobos/cli.py \
	   phobos/output.py \
	   phobos/core/__init__.py \
	   phobos/core/admin.py \
	   phobos/core/cfg.py \
	   phobos/core/const_module.c \
	   phobos/core/dss.py \
	   phobos/core/ffi.py \
	   phobos/core/glue_module.c \
	   phobos/core/ldm.py \
	   phobos/core/log.py \
	   phobos/core/store.py \
	   phobos/core/utils.py \
	   phobos/db/__init__.py \
	   phobos/db/__main__.py \
	   phobos/db/db_config.py \
	   phobos/db/sql/1.1/drop_schema.sql \
	   phobos/db/sql/1.1/schema.sql \
	   phobos/db/sql/1.2/drop_schema.sql \
	   phobos/db/sql/1.2/schema.sql \
	   phobos/db/sql/1.91/drop_schema.sql \
	   phobos/db/sql/1.91/schema.sql \
	   phobos/db/sql/1.92/drop_schema.sql \
	   phobos/db/sql/1.92/schema.sql \
	   scripts/phobos \
	   setup.py

all-local:
	$(PYTHON) setup.py build --verbose

clean-local:
	rm -f $(C_MODULES_SO)

pre-check-local:
	../tests/test_lock.sh

check-local: all-local pre-check-local
	(cd tests; ./run_tests.sh)
	rm -f /tmp/phobos_test.lock

.PHONY: all-local
