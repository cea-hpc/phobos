AM_CFLAGS= $(CC_OPT)

.PHONY: gen-proto
noinst_LTLIBRARIES=libpho_proto.la

srcdir=$(top_srcdir)/src
incdir=$(srcdir)/include
srldir=$(srcdir)/serializer
libpho_proto_la_SOURCES=pho_proto_common.proto pho_proto_lrs.proto

proto_headers=$(incdir)/pho_proto_common.pb-c.h $(incdir)/pho_proto_lrs.pb-c.h
proto_sources=$(srldir)/proto_common.pb-c.c $(srldir)/proto_lrs.pb-c.c

$(proto_headers): $(incdir)/%.pb-c.h: %.proto
	$(MAKE) gen-proto

$(proto_sources): $(srldir)/%.pb-c.c: pho_%.proto
	$(MAKE) gen-proto

gen-proto:
	protoc-c --c_out=. ${libpho_proto_la_SOURCES}
	mv *.pb-c.h $(incdir)
	for file in pho_*.pb-c.c ; do \
		mv $${file} $(srldir)/$${file#pho_} ; \
	done

