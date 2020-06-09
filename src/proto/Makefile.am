AM_CFLAGS= $(CC_OPT)

.PHONY: gen-proto.stamp
noinst_LTLIBRARIES=libpho_proto.la

srcdir=$(top_srcdir)/src
incdir=$(srcdir)/include
srldir=$(srcdir)/serializer
libpho_proto_la_SOURCES=pho_proto_common.proto pho_proto_lrs.proto

proto_headers=$(incdir)/pho_proto_common.pb-c.h $(incdir)/pho_proto_lrs.pb-c.h
proto_sources=$(srldir)/proto_common.pb-c.c $(srldir)/proto_lrs.pb-c.c

$(proto_headers): gen-proto.stamp
$(proto_sources): gen-proto.stamp

gen-proto.stamp: $(libpho_proto_la_SOURCES)
	protoc-c --c_out=. ${libpho_proto_la_SOURCES}
	mv *.pb-c.h $(incdir)
	for file in pho_*.pb-c.c ; do \
		mv $${file} $(srldir)/$${file#pho_} ; \
	done
	touch gen-proto.stamp

all-local: gen-proto.stamp

clean-local:
	rm -f $(srldir)/*.pb-c.c
	rm -f $(incdir)/*.pb-c.h
	rm -f gen-proto.stamp

