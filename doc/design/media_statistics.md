# Media Statistics

In order to prepare the repack mechanism, Phobos should be able to assess
whether a medium is eligible for a repack or not. This can be done by listing
the media with the most errors, the biggest ratio of deprecated to live objects
or the medium health (cf. `ltfs.mediaEfficiency`
[LTFS xattrs](doc/technical/ltfs_xattrs.md)).

Currently, each medium in the DSS has the following statistics associated (cf.
`struct media_stats`):
- `nb_obj`
- `logc_spc_used`
- `phys_spc_free`
- `phys_spc_used`
- `nb_load`
- `nb_errors`
- `last_load`

In version 1.94, `nb_load`, `nb_errors` and `last_load` are not updated by the
LRS. They can be set by `dss_media_set` with `DSS_SET_INSERT` or
`DSS_SET_UPDATE` as `dss_set_action`.

## Goals

We want to identify which are the best candidates for a repack. The goal of a
repack is to reduce the data fragmentation. We want to know the ratio of valid
to stale data on each tape and select the tape with the most fragmentation.

A tape may be repacked only if it is currently full or failed. We need to expose
the ratio of valid to stale data. Since Phobos supports versioning, depending
on the use case, a deprecated object may still be considered relevant.
Therefore, the term "stale data" refers to non deprecated objects AND objects
whose version are above a given threshold or that where deprecated after a
given date.

Computing these statistics on the fly for each entry in the media may be too
time consuming. We have to assess how long these queries would take and, if
necessary, update and store the deprecated volume in the database.

## CLI

A new command for medium will be added: `phobos <family> stats`
This command will display:

- `name`: the medium id;
- `adm_status`: the current `adm_status` from the media table (to know whether
  the medium is failed or not);
- `fs_status`: the current `fs_status` from the media table (to know whether the
  medium is full or not);
- `nb_errors`: the number of errors that this medium faced;
- `mediaEfficiency`: the mediaEfficiency stored in the LTFS xattr;
- `occupancy`: the occupancy rate of the medium (`log_spc_used / medium_size`).
  The medium size is the initial available space on the medium (eg.
  `phys_spc_free + phys_spc_used`);
- `log_spc_used`: the total logical volume of data stored;
- `phy_spc_used`: the total physical volume of data stored;
- `log_spc_deprecated`: the total logical volume of deprecated objects. This
  volume will be computed using criteria such as the number of versions or the
  date of deprecation;
- `deprecated_ratio`: `log_spc_deprecated / log_spc_used`;
- `nb_extents`: total number of extents
- `nb_extents_deprecated`: total number of deprecated extents. Custom criteria
  will also apply to this number;
- `deprecated_nb_ratio`: `nb_extents_deprecated / nb_extents`.

`phobos tape stats --help` should display each fields and their meaning.

For example (not all fields are displayed):
```
$ phobos tape stats
| name | adm_status | nb errors | mediaEfficiency | occupancy | log_spc_used |
|------|------------|-----------|-----------------|-----------|--------------|
| T1   | failed     | 1         | 0x0a            | 50%       | 8TB          |
```
Note: `mediaEfficiency` is in hexa in this example since the LTFS xattr is.
There is no direct equivalent for dirs.

### Options

The fields of other `list` commands:

- `-o`: select which columns to display
- `-f`: select the output format

Specify query options:

- `--live-until-version <n>`: in the deprecated statistics, do not count objects
  younger than version `<n>` included;
- `--live-during <t>`: in the deprecated statistics, do not count objects
  that are deprecated since less than `<t>`. Format: `<n>[s|m|h|d|w|M|Y]`;
- `-T|--tags`: comma-separated list of tags to filter;
- `-S|--status`: comma-separated list of statuses: any value of `adm_status` and
  `fs_status`;
- `res`: list of resources to query (support clustershell syntax).

Additional queries can be added later as it may be more efficient to do them in
SQL directly:

- `--sort|-s <field>`: sort option on a given numeric field
- `--rsort|-r <field>`: same as sort but in the reverse order
- `--nb-entries|-N`: the number of entries to return:

### Example

Display all the tapes sorted first by `occupancy`, then by `deprecated_ratio`.
Deprecated volume is the total logical volume of objects that have been
deprecated for more than a week. Tapes with the highest ratio will be displayed
first.

```
phobos tape stats --live-during 1w \
                  --rsort occupancy \
                  --rsort deprecated_ratio \
                  --output name
```

## DSS API

```
/* deprecated_ratio, deprecated_nb_ratio and occupancy can be computed from
 * these information.
 */
struct enriched_media_stats {
    struct pho_resource rsc; /* name and adm_status */
    struct media_stats  media_stats; /* logc_spc_used, nb_errors, medium_size,
                                      * nb_extents, mediaEfficiency,
                                      * phy_spc_used
                                      */
    enum fs_status      status; /* FULL, EMPTY, USED, BLANK */
    ssize_t             log_spc_deprecated;
    size_t              nb_extents_deprecated;
};

struct stats_filter {
    struct timeval     deprec_time_limit; /* time before which an object is
                                           * considered deprecated. Ignored if
                                           * set to 0.
                                           */
    int                deprec_version_limit; /* version number before which an
                                              * object is considered deprecated.
                                              * Objects in the object table are
                                              * always considered alive. Ignored
                                              * if negative since this is not a
                                              * valid version number.
                                              */
    bool              *reverse_sort; /* reverse_sort[i] is true if we should
                                      * sort the i-th field by descending order,
                                      * false otherwise
                                      */
    char             **fields; /* fields[i] indicates the name of the i-th field
                                * to sort
                                */
    size_t             nb_fields; /* size of \p reverse_sort and \p fields. If
                                   * 0, no sorting should be done.
                                   */
    size_t             max_nb_results; /* maximum number of entries to query */
};

/**
 * List enriched media statistics from the DSS
 *
 * \param[in]  hdl        valid connection handle
 * \param[in]  filter     additional query information
 * \param[out] med_list   enriched media statistics
 * \param[out] med_count  number of elements in \p med_list
 *
 * \return     0 on success, negative POSIX error code on failure
 */
int dss_media_enriched_stats(struct dss_handle *hdl,
                             const stats_filter *filter,
                             struct enriched_media_stats **med_list,
                             int *med_count);
```

Note: the fields `reverse_sort`, `fields`, `nb_fields` and `max_nb_results` may
be added later.

This function will require a new `enum dss_type`: `DSS_ENRICHED_STATS` and the
associated functions to build the `struct enriched_media_stats` from PostgreSQL
results.

The `filter` field will be useful for specifying restrictions on the query:
- the number of results to retrieve
- whether to sort the results or not and how

## Proposed solution

1. Make sure that the LRS updates all the relevant information that are stored
   in the DSS.
   - the `fs_status` of a tape should be set to FULL correctly:
     - when a tape is flagged as readonly on mount;
     - when a client encounters an `ENOSPC` error;
     - when a sync on the LRS returns `ENOSPC`.
     - when a given threshold of occupancy is reached. This threshold should be
       added to the configuration;
   - the `nb_errors` should be updated each time a tape is set to `failed`;
   - add the `medium_size` as a new entry in the media table which is set only
     when the medium is first added;
   - update `last_load` each time a medium is loaded:
     - for dir and rados, they may only be modified when phobos starts since
       they are always considered loaded;
   - `mediaEfficiency`;
   - rename `nb_obj` into `nb_extents` in the `media_stats`
2. Add a function to query statistics from the DSS:
   - `dss_media_stats`
3. Add a command to query the statistics of each medium:
   `phobos <tape|dir> stats`
4. Performance: if the query to list all the media turns out to be too slow, we
   need to store and update the deprecated statistics in the DSS and simply list
   them when necessary.
   - we should at least scale up to 30k extents per medium with 20k media.
   - to reduce the cost we can:
     - store and update the deprecated volume in the DSS, identify the most
       relevent medium for repack using these stats and compute finer
       statistics on the those medium. This would result in 2 DSS queries but
       simpler ones;
     - store and update the deprecated count using more advanced criteria such
       as which versions are considered valid. The value to use would be set in
       the configuration. This reduces the number of queries to one but the
       criteria is fixed by the configuration and has to stay the same unless we
       provide a tool to recompute every count with a new criteria.
   - in both solutions, we can further reduce the cost by limiting the number of
     statistics to compute.
