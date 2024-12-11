AM_CFLAGS= $(CC_OPT)

C_MODULES_SO=phobos/core/const.so \
	     phobos/core/glue.so

EXTRA_DIST=phobos/__init__.py \
	   phobos/output.py \
	   phobos/cli/__init__.py \
	   phobos/cli/common/__init__.py \
	   phobos/cli/common/args.py \
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
	   phobos/db/sql/1.93/drop_schema.sql \
	   phobos/db/sql/1.93/schema.sql \
	   phobos/db/sql/1.95/drop_schema.sql \
	   phobos/db/sql/1.95/schema.sql \
	   phobos/db/sql/2.0/drop_schema.sql \
	   phobos/db/sql/2.0/schema.sql \
	   phobos/db/sql/2.1/drop_schema.sql \
	   phobos/db/sql/2.1/schema.sql \
	   phobos/db/sql/2.2/drop_schema.sql \
	   phobos/db/sql/2.2/schema.sql \
	   scripts/phobos \
	   setup.py

if RADOS_ENABLED
RUN_TESTS=./run_tests.sh rados
else
RUN_TESTS=./run_tests.sh
endif

all-local:
	$(PYTHON) setup.py build --verbose

clean-local:
	rm -f $(C_MODULES_SO)

check-local: all-local
	(cd tests; $(RUN_TESTS))

.PHONY: all-local
