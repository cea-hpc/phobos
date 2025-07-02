CREATE EXTENSION IF NOT EXISTS "uuid-ossp";

CREATE TYPE dev_family AS ENUM ('tape', 'dir', 'rados_pool');
CREATE TYPE adm_status AS ENUM ('locked', 'unlocked', 'failed');
CREATE TYPE fs_type AS ENUM ('POSIX', 'LTFS', 'RADOS');
CREATE TYPE address_type AS ENUM ('PATH', 'HASH1', 'OPAQUE');
CREATE TYPE fs_status AS ENUM ('blank', 'empty', 'used', 'full', 'importing');
CREATE TYPE extent_state AS ENUM ('pending','sync','orphan');
CREATE TYPE lock_type AS ENUM('object', 'device', 'media', 'media_update',
                              'extent');
CREATE TYPE operation_type AS ENUM ('Library scan', 'Library open',
                                    'Device lookup', 'Medium lookup',
                                    'Device load', 'Device unload',
                                    'LTFS mount', 'LTFS umount',
                                    'LTFS format', 'LTFS df',
                                    'LTFS sync');
CREATE TYPE copy_status AS ENUM ('incomplete', 'readable', 'complete');

-- to extend enums: ALTER TYPE type ADD VALUE 'value'

-- Database schema information
CREATE TABLE schema_info (
    version         varchar(32) PRIMARY KEY
);

-- Insert current schema version
INSERT INTO schema_info VALUES ('3.2');

CREATE TABLE device(
    family          dev_family,
    model           varchar(32),
    id              varchar(255),
    host            varchar(128),
    adm_status      adm_status,
    path            varchar(256),
    library         varchar(255) NOT NULL,

    PRIMARY KEY (family, id, library)
);
CREATE INDEX ON device USING gin(host);

CREATE TABLE media(
    family          dev_family,
    model           varchar(32),
    id              varchar(255),
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
    library         varchar(255) NOT NULL,
    groupings       jsonb, -- json array (optimized for searching)

    PRIMARY KEY (family, id, library)
);
CREATE INDEX ON media((stats->>'phys_spc_free'));

CREATE TABLE object(
    oid             varchar(1024),
    user_md         jsonb,
    object_uuid     varchar(36) UNIQUE DEFAULT uuid_generate_v4(),
    version         integer DEFAULT 1 NOT NULL,
    creation_time   timestamp DEFAULT now(),
    _grouping       varchar(255),
    -- grouping word is already used by psql as a function
    -- _grouping will be replaced by groupings in the future if we want
    -- to manage more than one grouping per object

    PRIMARY KEY (oid)
);

CREATE TABLE deprecated_object(
    oid             varchar(1024),
    object_uuid     varchar(36),
    version         integer DEFAULT 1 NOT NULL,
    user_md         jsonb,
    deprec_time     timestamp DEFAULT now(),
    creation_time   timestamp DEFAULT now(),
    _grouping       varchar(255),
    -- grouping word is already used by psql as a function
    -- _grouping will be replaced by groupings in the future if we want
    -- to manage more than one grouping per object

    PRIMARY KEY (object_uuid, version)
);

CREATE TABLE extent(
    extent_uuid     varchar(36) UNIQUE DEFAULT uuid_generate_v4(),
    state           extent_state,
    size            bigint,
    medium_family   dev_family,
    medium_id       varchar(255),
    address         varchar(1024),
    hash            jsonb,
    info            jsonb,
    offsetof        bigint, -- the name 'offset' is a reserved keyword
    medium_library  varchar(255) NOT NULL,

    PRIMARY KEY (extent_uuid)
);

CREATE TABLE layout(
    object_uuid     varchar(36),
    version         integer DEFAULT 1 NOT NULL,
    extent_uuid     varchar(36),
    layout_index    integer,
    copy_name       varchar(1024),

    PRIMARY KEY (object_uuid, version, layout_index, copy_name)
);

CREATE TABLE lock(
    type            lock_type,
    id              varchar(2048),
    hostname        varchar(256) NOT NULL,
    owner           integer NOT NULL,
    timestamp       timestamp DEFAULT now(),
    is_early        boolean DEFAULT FALSE,
    last_locate     timestamp DEFAULT NULL,

    PRIMARY KEY (type, id)
);

CREATE TABLE logs(
    family    dev_family,
    device    varchar(255),
    medium    varchar(255),
    uuid      varchar(36) UNIQUE DEFAULT uuid_generate_v4(),
    errno     integer NOT NULL,
    cause     operation_type,
    message   jsonb,
    time      timestamp DEFAULT now(),
    library   varchar(255) NOT NULL,

    PRIMARY KEY (uuid)
);

CREATE TABLE copy(
    object_uuid     varchar(36),
    version         integer DEFAULT 1 NOT NULL,
    copy_name       varchar(1024),
    lyt_info        jsonb,
    copy_status     copy_status DEFAULT 'incomplete',
    creation_time   timestamp DEFAULT now(),
    access_time     timestamp DEFAULT now(),

    PRIMARY KEY (object_uuid, version, copy_name)
);
