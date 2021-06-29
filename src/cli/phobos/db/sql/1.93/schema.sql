CREATE TYPE dev_family AS ENUM ('disk', 'tape', 'dir');
CREATE TYPE adm_status AS ENUM ('locked', 'unlocked', 'failed');
CREATE TYPE fs_type AS ENUM ('POSIX', 'LTFS');
CREATE TYPE address_type AS ENUM ('PATH', 'HASH1', 'OPAQUE');
CREATE TYPE fs_status AS ENUM ('blank', 'empty', 'used', 'full');
CREATE TYPE extent_state AS ENUM ('pending','sync','orphan');
CREATE TYPE lock_type AS ENUM('object', 'device', 'media');

-- to extend enums: ALTER TYPE type ADD VALUE 'value'

-- Database schema information
CREATE TABLE schema_info (
    version         varchar(32) PRIMARY KEY
);

-- Insert current schema version
INSERT INTO schema_info VALUES ('1.93');

CREATE TABLE device(
    family          dev_family,
    model           varchar(32),
    id              varchar(255) UNIQUE,
    host            varchar(128),
    adm_status      adm_status,
    path            varchar(256),

    PRIMARY KEY (family, id)
);
CREATE INDEX ON device USING gin(host);

CREATE TABLE media(
    family          dev_family,
    model           varchar(32),
    id              varchar(255) UNIQUE,
    adm_status      adm_status,
    fs_type         fs_type,
    fs_label        varchar(32),
    address_type    address_type,
    fs_status       fs_status,
    stats           jsonb,
    tags            jsonb, -- json array (optimized for searching)
    put             boolean DEFAULT TRUE,
    get             boolean DEFAULT TRUE,
    delete          boolean DEFAULT TRUE,

    PRIMARY KEY (family, id)
);
CREATE INDEX ON media((stats->>'phys_spc_free'));

CREATE TABLE object(
    oid             varchar(1024),
    uuid            varchar(36) UNIQUE DEFAULT uuid_generate_v4(),
    version         integer DEFAULT 1 NOT NULL,
    user_md         jsonb,

    PRIMARY KEY (oid)
);

CREATE TABLE deprecated_object(
    oid             varchar(1024),
    uuid            varchar(36),
    version         integer DEFAULT 1 NOT NULL,
    user_md         jsonb,
    deprec_time     timestamp DEFAULT now(),

    PRIMARY KEY (uuid, version)
);

CREATE TABLE extent(
    oid             varchar(1024),
    uuid            varchar(36),
    version         integer DEFAULT 1 NOT NULL,
    state           extent_state,
    lyt_info        jsonb,
    extents         jsonb,

    PRIMARY KEY (uuid, version)
);

CREATE TABLE lock(
    type            lock_type,
    id              varchar(2048),
    hostname        varchar(256) NOT NULL,
    owner           integer NOT NULL,
    timestamp       timestamp DEFAULT now(),

    PRIMARY KEY (type, id)
);

CREATE OR REPLACE FUNCTION extents_mda_idx(extents jsonb) RETURNS text[] AS
$$
    SELECT array_agg(value#>>'{media}')::text[]
    FROM jsonb_array_elements(extents);
$$ LANGUAGE SQL IMMUTABLE;

CREATE INDEX extents_mda_id_idx ON extent
    USING GIN (extents_mda_idx((extent.extents)));
