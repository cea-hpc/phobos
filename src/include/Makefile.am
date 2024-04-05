SUFFIXES = .pb-c.h

protodir=../proto
proto_headers=pho_proto_common.pb-c.h pho_proto_lrs.pb-c.h pho_proto_tlc.pb-c.h
noinst_HEADERS=pho_cache.h \
               pho_cfg.h \
               pho_comm.h \
               pho_common.h \
               pho_daemon.h \
               pho_dss.h \
               pho_dss_wrapper.h \
               pho_io.h \
               pho_layout.h \
               pho_mapper.h \
               pho_module_loader.h \
               pho_ref.h \
               pho_srl_common.h \
               pho_srl_lrs.h \
               pho_srl_tlc.h \
               pho_types.h \
               pho_type_utils.h \
               slist.h \
               $(proto_headers)

# Required headers:
#   - phobos_store.h  main program interface
#   - phobos_admin.h  admin command interface
#   - pho_attrs.h     access and manipulate object metadata
include_HEADERS=pho_attrs.h pho_cfg.h pho_comm.h pho_common.h pho_dss.h \
                pho_dss_wrapper.h pho_ldm.h pho_types.h phobos_store.h \
                phobos_admin.h

$(proto_headers): $(protodir)/gen-proto.stamp

$(protodir)/gen-proto.stamp:
	$(MAKE) -C $(protodir) gen-proto.stamp
