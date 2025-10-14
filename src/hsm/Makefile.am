AM_CFLAGS= $(CC_OPT)

sbin_PROGRAMS=phobos_hsm_sync_dir phobos_hsm_release_dir

phobos_hsm_sync_dir_SOURCES=hsm_sync_dir.c
phobos_hsm_sync_dir_CFLAGS=$(AM_CFLAGS)
phobos_hsm_sync_dir_LDADD=../core/libpho_core.la ../ldm/libpho_ldm.la \
                          ../store/libphobos_store.la
phobos_hsm_sync_dir_LDFLAGS=-Wl,-rpath=$(libdir) -Wl,-rpath=$(pkglibdir)

phobos_hsm_release_dir_SOURCES=hsm_release_dir.c
phobos_hsm_release_dir_CFLAGS=$(AM_CFLAGS)
phobos_hsm_release_dir_LDADD=../core/libpho_core.la ../ldm/libpho_ldm.la \
                             ../store/libphobos_store.la
phobos_hsm_release_dir_LDFLAGS=-Wl,-rpath=$(libdir) -Wl,-rpath=$(pkglibdir)
