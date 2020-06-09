AM_CFLAGS= $(CC_OPT)

noinst_LTLIBRARIES=libpho_serializer.la

protodir=../proto
proto_sources=proto_common.pb-c.c proto_lrs.pb-c.c
libpho_serializer_la_SOURCES=srl_lrs.c $(proto_sources)
libpho_serializer_la_LIBADD=-lprotobuf-c

$(proto_sources): $(protodir)/gen-proto.stamp
srl_rls.lo: $(protodir)/gen-proto.stamp

$(protodir)/gen-proto.stamp:
	$(MAKE) -C $(protodir) gen-proto.stamp
