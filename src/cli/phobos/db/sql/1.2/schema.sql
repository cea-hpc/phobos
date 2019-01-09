CREATE TYPE dev_family AS ENUM ('disk', 'tape', 'dir');
CREATE TYPE adm_status AS ENUM ('locked', 'unlocked', 'failed');
CREATE TYPE fs_type AS ENUM ('POSIX', 'LTFS');
CREATE TYPE address_type AS ENUM ('PATH', 'HASH1', 'OPAQUE');
CREATE TYPE fs_status AS ENUM ('blank', 'empty', 'used', 'full');
CREATE TYPE extent_state AS ENUM ('pending','sync','orphan');

-- to extend enums: ALTER TYPE type ADD VALUE 'value'

-- Database schema information
CREATE TABLE schema_info (
    version         varchar(32) PRIMARY KEY
);

-- Insert current schema version
INSERT INTO schema_info VALUES ('1.2');

CREATE TABLE device(
    family          dev_family,
    model           varchar(32),
    id              varchar(32) UNIQUE,
    host            varchar(128),
    adm_status      adm_status,
    path            varchar(256),
    lock            varchar(256) DEFAULT '' NOT NULL,
    lock_ts         bigint,

    PRIMARY KEY (family, id)
);
CREATE INDEX ON device USING gin(host);

CREATE TABLE media(
    family          dev_family,
    model           varchar(32),
    id              varchar(32) UNIQUE,
    adm_status      adm_status,
    fs_type         fs_type,
    fs_label        varchar(32) DEFAULT '' NOT NULL,
    address_type    address_type,
    fs_status       fs_status,
    lock            varchar(256) DEFAULT '' NOT NULL,
    lock_ts         bigint,
    stats           jsonb,
    tags            jsonb, -- json array (optimized for searching)

    PRIMARY KEY (family, id)
);
CREATE INDEX ON media((stats->>'phys_spc_free'));

CREATE TABLE object(
    oid             varchar(1024),
    user_md         jsonb,

    PRIMARY KEY (oid)
);

CREATE TABLE extent(
    oid             varchar(1024),
    state           extent_state,
    lyt_info        jsonb,
    extents         jsonb,

    PRIMARY KEY (oid)
);

CREATE OR REPLACE FUNCTION extents_mda_idx(extents jsonb) RETURNS text[] AS
$$
    SELECT array_agg(value#>>'{media}')::text[]
    FROM jsonb_array_elements(extents);
$$ LANGUAGE SQL IMMUTABLE;

CREATE INDEX extents_mda_id_idx ON extent
    USING GIN (extents_mda_idx((extent.extents)));
