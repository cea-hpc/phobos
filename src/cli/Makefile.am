AM_CFLAGS= $(CC_OPT)

C_MODULES_SO=phobos/core/const.so \
	     phobos/core/glue.so

EXTRA_DIST=phobos/__init__.py \
	   phobos/output.py \
	   phobos/cli/__init__.py \
	   phobos/cli/action/__init__.py \
	   phobos/cli/action/add.py \
	   phobos/cli/action/clean.py \
	   phobos/cli/action/clear.py \
	   phobos/cli/action/create.py \
	   phobos/cli/action/delete.py \
	   phobos/cli/action/dump.py \
	   phobos/cli/action/fair_share.py \
	   phobos/cli/action/format.py \
	   phobos/cli/action/get.py \
	   phobos/cli/action/getmd.py \
	   phobos/cli/action/list.py \
	   phobos/cli/action/locate.py \
	   phobos/cli/action/lock.py \
	   phobos/cli/action/mput.py \
	   phobos/cli/action/ping.py \
	   phobos/cli/action/put.py \
	   phobos/cli/action/rename.py \
	   phobos/cli/action/resource_delete.py \
	   phobos/cli/action/status.py \
	   phobos/cli/action/undelete.py \
	   phobos/cli/action/unlock.py \
	   phobos/cli/common/__init__.py \
	   phobos/cli/common/args.py \
	   phobos/cli/common/exec.py \
	   phobos/cli/common/utils.py \
	   phobos/cli/target/__init__.py \
	   phobos/cli/target/copy.py \
	   phobos/cli/target/device.py \
	   phobos/cli/target/dir.py \
	   phobos/cli/target/drive.py \
	   phobos/cli/target/extent.py \
	   phobos/cli/target/lib.py \
	   phobos/cli/target/lock.py \
	   phobos/cli/target/logs.py \
	   phobos/cli/target/media.py \
	   phobos/cli/target/object.py \
	   phobos/cli/target/phobosd.py \
	   phobos/cli/target/sched.py \
	   phobos/cli/target/store.py \
	   phobos/cli/target/rados.py \
	   phobos/cli/target/tape.py \
	   phobos/cli/target/tlc.py \
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
	   phobos/db/sql/3.0/drop_schema.sql \
	   phobos/db/sql/3.0/schema.sql \
	   phobos/db/sql/3.2/drop_schema.sql \
	   phobos/db/sql/3.2/schema.sql \
	   scripts/phobos \
	   setup.py

bin_SCRIPTS=scripts/phobos

if RADOS_ENABLED
RUN_TESTS=./run_tests.sh rados
else
RUN_TESTS=./run_tests.sh
endif

if NO_RPM_BUILD
ROOT_BUILD_OPT=
else
ROOT_BUILD_OPT=--root $(RPM_BUILD_ROOT)
endif

all-local:
	$(PYTHON) setup.py build --verbose

clean-local:
	rm -f $(C_MODULES_SO)

check-local: all-local
	(cd tests; $(RUN_TESTS))

install-exec-local:
	$(PYTHON) setup.py install --skip-build $(ROOT_BUILD_OPT)

.PHONY: all-local
