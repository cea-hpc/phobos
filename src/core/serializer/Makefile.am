AM_CFLAGS= $(CC_OPT)

incdir=$(top_srcdir)/src/include

proto_raw=pho_proto_common.proto pho_proto_lrs.proto pho_proto_tlc.proto

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
