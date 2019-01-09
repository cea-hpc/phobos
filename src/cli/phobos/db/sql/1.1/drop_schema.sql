DROP TABLE IF EXISTS
    device,
    media,
    object,
    extent CASCADE;

DROP TYPE IF EXISTS
    dev_family,
    dev_model,
    tape_model,
    fs_status,
    adm_status,
    fs_type,
    address_type,
    extent_state CASCADE;

DROP FUNCTION IF EXISTS extents_mda_idx(jsonb) CASCADE;
