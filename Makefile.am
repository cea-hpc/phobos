ACLOCAL_AMFLAGS = -I autotools/m4

SUBDIRS=src tests tests/externs/cli tests/interns

rpm_dir=`pwd`/rpms

rpm: dist phobos.spec
	mkdir -p $(rpm_dir)/BUILD $(rpm_dir)/SPECS $(rpm_dir)/SRPMS $(rpm_dir)/RPMS
	rpmbuild --define="_topdir $(rpm_dir)" -ta $(distdir).tar.gz

EXTRA_DIST= phobos.spec phobos.spec.in scripts/pho_ldm_helper \
	    scripts/phobos_db README.md doc/cfg/template.conf

sbin_SCRIPTS=scripts/pho_ldm_helper \
             scripts/phobos_db
