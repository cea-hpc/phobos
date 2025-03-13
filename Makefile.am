ACLOCAL_AMFLAGS = -I autotools/m4

SUBDIRS=. src tests tests/externs/api tests/externs/cli tests/interns \
        tests/prod tape_library_certification doc/man

rpm_dir=`pwd`/rpms

options =

if RADOS_ENABLED
options += --with=rados
endif

rpm: dist phobos.spec
	mkdir -p $(rpm_dir)/BUILD $(rpm_dir)/SPECS $(rpm_dir)/SRPMS $(rpm_dir)/RPMS
	rpmbuild $(options) --define="_topdir $(rpm_dir)" -ta $(distdir).tar.gz

man:
	$(MAKE) -C doc/man

EXTRA_DIST= phobos.spec phobos.spec.in scripts/pho_ldm_helper \
	    scripts/phobos_db README.md doc/cfg/template.conf $(dist_man_MANS) \
	    doc/man/source

sbin_SCRIPTS=scripts/pho_ldm_helper \
             scripts/phobos_db

check-local:
	./tests/test_lock.sh

CLEANFILES = /tmp/phobos_test.lock
