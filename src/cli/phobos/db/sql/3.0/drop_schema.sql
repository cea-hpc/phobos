DROP TABLE IF EXISTS
    schema_info,
    device,
    media,
    object,
    deprecated_object,
    layout,
    extent,
    lock,
    logs,
    copy CASCADE;

DROP TYPE IF EXISTS
    dev_family,
    fs_status,
    adm_status,
    fs_type,
    address_type,
    extent_state,
    lock_type,
    operation_type,
    copy_status CASCADE;
