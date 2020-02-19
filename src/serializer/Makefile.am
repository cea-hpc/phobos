AM_CFLAGS= $(CC_OPT)

noinst_LTLIBRARIES=libpho_serializer.la

protodir=$(top_srcdir)/src/proto
proto_sources=proto_common.pb-c.c proto_lrs.pb-c.c
libpho_serializer_la_SOURCES=srl_lrs.c $(proto_sources)
libpho_serializer_la_LIBADD=-lprotobuf-c

$(proto_sources): %.pb-c.c: $(protodir)/pho_%.proto
	$(MAKE) -C $(protodir) gen-proto
