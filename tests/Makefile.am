AM_CFLAGS= $(CC_OPT) @CMOCKA_CFLAGS@
AM_LDFLAGS=@CMOCKA_LIBS@

TO_SRC=../src

CORE_LIB=$(TO_SRC)/core/libpho_core.la
DAEMON_LIB=$(TO_SRC)/daemon/libpho_daemon.la
MOD_LOAD_LIB=$(TO_SRC)/module-loader/libpho_module_loader.la
IO_LIB=$(TO_SRC)/io/libpho_io.la $(MOD_LOAD_LIB)
LDM_LIB=$(TO_SRC)/ldm/libpho_ldm.la $(MOD_LOAD_LIB)
LRS_LIB=$(TO_SRC)/lrs/libpho_lrs.la
SCSI_LIB=$(TO_SRC)/tlc/scsi/libpho_scsi.la
TLC_LIB=$(TO_SRC)/tlc/libpho_tlc.la

noinst_LTLIBRARIES=libpho_test_utils.la

libpho_test_utils_la_SOURCES=pho_test_utils.c pho_test_utils.h test_setup.c \
                             test_setup.h pho_test_xfer_utils.h
libpho_test_utils_la_LIBADD=$(LRS_LIB) $(LDM_LIB) $(IO_LIB) $(CORE_LIB) \
                            $(DAEMON_LIB) $(SCSI_LIB) $(TLC_LIB)
libpho_test_utils_la_CFLAGS=${AM_CFLAGS} -I$(TO_SRC)/lrs \
                            -I$(TO_SRC)/ldm-modules -I$(TO_SRC)/tlc \
                            -I$(TO_SRC)/tlc/scsi
