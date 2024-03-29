DROP TABLE IF EXISTS
    schema_info,
    device,
    media,
    object,
    deprecated_object,
    extent,
    lock,
    logs CASCADE;

DROP TYPE IF EXISTS
    dev_family,
    fs_status,
    adm_status,
    fs_type,
    address_type,
    extent_state,
    lock_type,
    operation_type CASCADE;

DROP FUNCTION IF EXISTS extents_mda_idx(jsonb) CASCADE;
