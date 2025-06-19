AM_CFLAGS= $(CC_OPT)

noinst_LTLIBRARIES=libpho_serializer.la

incdir=$(top_srcdir)/src/include

proto_raw=pho_proto_common.proto pho_proto_lrs.proto pho_proto_tlc.proto
proto_headers=$(incdir)/pho_proto_common.pb-c.h \
    $(incdir)/pho_proto_lrs.pb-c.h \
    $(incdir)/pho_proto_tlc.pb-c.h

proto_sources=proto_common.pb-c.c proto_lrs.pb-c.c proto_tlc.pb-c.c $(proto_raw)

libpho_serializer_la_SOURCES=srl_lrs.c srl_tlc.c $(proto_sources)
libpho_serializer_la_LIBADD=-lprotobuf-c

$(proto_sources): gen-proto.stamp
srl.lo: gen-proto.stamp

gen-proto.stamp: ${proto_raw}
	protoc-c --c_out=. ${proto_raw}
	mv *.pb-c.h ${incdir}
	for file in pho_*.pb-c.c ; do \
		mv $${file} $${file#pho_} ; \
	done
	touch gen-proto.stamp

all-local: gen-proto.stamp

clean-local:
	rm -f /*.pb-c.c
	rm -f ${incdir}/*.pb-c.h
	rm -f gen-proto.stamp
