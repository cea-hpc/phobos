SUFFIXES = .pb-c.h

protodir=$(top_srcdir)/src/proto
proto_headers=pho_proto_common.pb-c.h pho_proto_lrs.pb-c.h
noinst_HEADERS=pho_cfg.h pho_comm.h pho_common.h pho_dss.h pho_io.h \
	       pho_layout.h pho_ldm.h pho_lrs.h pho_mapper.h pho_srl_lrs.h \
	       pho_type_utils.h pho_types.h $(proto_headers)

# Required headers:
#   - phobos_store.h  main program interface
#   - pho_attrs.h     access and manipulate object metadata
include_HEADERS=pho_attrs.h phobos_store.h

$(proto_headers): %.pb-c.h: $(protodir)/%.proto
	make -C $(protodir) gen-proto
