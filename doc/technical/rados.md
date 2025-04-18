# RADOS I/O adapter

Library needed: librados

## General adapter requirements

* Adapters should provide an object-based interface: put/get/del. Adapter
  interface to other layers must be available as a vector of functions.

* An adapter must be able to locate any extent by its `address`. The `address`
  is an internal representation (e.g. a POSIX path or an opaque object key) made
  of an `extent_key` and an `extent_description` string. This address is unique
  thanks to the uniqueness of the `extent_key`.

* For sysadmin convenience, the adapter should keep the human-readable
  `extent_description` in the object address, if possible. It can however
  complete it with other internal representations (eg. a hash). These addressing
  components can however be modified for technical reasons or performance
  considerations.  For instance, the `extent_description` can be truncated, '/'
  characters can be changed to another delimiter, non_printable character can be
  replaced, etc...

* On a put, Phobos provides the `extent_key` and the `extent_description` and
  retrieves the generated `address` of the extent.

* On a get or delete, Phobos gets back the extent from the adapter by
  providing the corresponding registered `address`.

* Adapters should provide a way to attach an arbitrary set of metadata to
  extents (e.g. as extended attributes, or a metadata blob). This information is
  critical to rebuild the Phobos database in case of accidental loss.

* If possible, it must provide a feature to manage media transactions,
  that could be performed after writing an extent or after writing a set of
  files.

## RADOS information

### Ceph and RADOS

* RADOS (Reliable Autonomic Distributed Object Store) is an object storage
  service.

* Ceph is a software-defined storage solution which unifies different types of
  storage (block, object, file) in one platform.

* RADOS is used for the object part of Ceph but also for the librados api
  to create customized Ceph clients.

* A Ceph cluster is a combination of object storage devices and daemons managing
  stored data.

### Requirements

* In order to be compiled with gcc the "-lrados" option is required.

* A Ceph cluster has to be online.

### Ceph Setup

* There are 2 main ways to install and setup Ceph:

  + Install an orchestration tool like Cephadm or Rook and install Ceph
    with it. Setting up Ceph is almost automatic since those tools use
    docker or podman to set up nodes for the Ceph cluster. An internet
    connection is mandatory to do this. [14]

  + Manually install Ceph

    - Depending on the OS, you can install Ceph directly
      from the OS's official repository. On CentOS, you have to install an
      intermediate package that will create a repository. Then an internet
      access is needed to install Ceph. Another option is to directly download
      every Ceph packages from https://download.ceph.com/ and install them one
      by one. [14]

    - After installing Ceph, a cluster can be manually set up using Ceph
      commands. The ceph-deploy package provides commands to setup a minimal
      Ceph cluster quite quickly. Note that ceph-deploy does not support RHEL8,
      CentOS 8 or newer operating systems. It is possible to find rpm packages
      to install Ceph on an offline machine. Unfortunately, on CentOS 7 Ceph
      requires two packages which have to be installed with pip (pecan and
      werkzeug). [15], [16]

* The status of the cluster can be checked with the `ceph -s` command, which
  requires root access because the Ceph configuration file is at the location
  `/etc/ceph/<Ceph cluster's name>.conf` by default.

* If a warning about an insecure mode shows up after setting up the cluster, the
  insecure mode can be disabled with the command:
  `sudo ceph config set mon auth_allow_insecure_global_id_reclaim false`.
  This command enables a secure mode that came with Ceph14.2.20 to solve a
  security vulnerability in the Ceph authentication framework.

### Daemons in Ceph

* There are 4 types of Ceph daemons:

    - The Ceph Monitor, whichs provides a view on the cluster map, and manages
      the authentification between daemons and clients.
    - The Ceph OSD (Object Storage Devices) Daemon, which stores data as objects
      on a storage node, handles data replication, recovery and rebalancing and
      reports the node's state to Ceph monitors.
    - The Ceph Metadata Server (MDS), which manages file metadata when CephFS
      (Ceph File System) is used to provide file services.
    - The Ceph Manager, which acts as an endpoint for monitoring, cluster
      orchestration, and plug-in modules. This is not used for block or object
      storages.
      Clusters can be orchestrated by external services (Rook, Cephadm, etc) to
      monitor available hardware, create and destroy OSDs and run MDS or RADOS
      gateway (S3/Swift gateway) services.[12], [1], [11]

* A minimal Ceph cluster has to have at least one Ceph Monitor and two Ceph OSD
  Daemons.

* Librados can be used to connect to two types of daemons in a Ceph Storage
  Cluster: Ceph monitors and Ceph OSD daemons.

### OSDs

* OSDs can be backed by a single storage device (HDD or SSD) or a combination of
  devices: for example, a HDD for most data and an SSD (or partition of SSD)
  for some metadata.
  The number of OSDs usually depends on the amount of data to be stored,
  the size of each storage device, and the level and type of redundancy
  specified. [6]

* OSDs have two backends:
  - BlueStore: The default backend, which uses raw block devices/partitions and
    does not need to create a partition or a disk using a file system.
  - FileStore: It relies on a standard file system on top of the raw
    devices/partitions.
    It includes size limitations on extended attributes depending on the file
    system used (4KB total on ext4, for instance). [8]

### Data placement in Ceph

* Ceph stores, replicates and rebalances data objects across a Ceph cluster
  dynamically.[3]

* Placement Groups are an internal implementation detail of how Ceph distributes
  data.
  An object is placed in a placement group depending on the hash of the object
  ID, the number of placement groups in the defined pool and the ID of the pool.
  They reduce the amount of metadata per object when Ceph stores the data in
  Object Storage Daemons. [3], [4]

* Data is stored within pools. Each pool has a number of placement groups,
  replicas and a CRUSH (Controlled Replication Under Scalable Hashing) rule.
  CRUSH rules are the data distribution policy. They can be created with the
  `ceph osd crush rule` command.
  There are 3 types of CRUSH rules:
    - Erasure CRUSH rules for erasure coded pools, tied to an erasure coding
      profile.
    - Replicated CRUSH rules for replicated pools, tied to a root node the
      replicated pool has to start from, a type of failure domain that defines
      the way replicas are stored and optionnaly, the type of devices to use
      (ssd or hdd).
    - Simple CRUSH rules for simple pools. They have a root node, a type of
      failure domain and an optional mode: 'firstn' (the default one) and
      'indep' (best for erasure pools).
  Pools are tied to RADOS permissions. Only authentificated users with the right
  permissions can store data in a pool.

* A pool can have several placement groups. Placement groups can be linked to
  several OSDs. OSDs can be shared between several placement groups. [4], [7]

* CRUSH maps are used to pseudo-randomly map data to OSDs. Data is distributed
  accross the different OSDs according to the configured replication policy and
  failure domain. [5]

* The balance is a feature that automatically optimize the distribution of
  placement groups across devices to balance data distribution and workload
  distribution across OSDs. [2]

* Only pools can be managed through LIBRADOS.

### Connection to the Ceph storage

* Librados provides a cluster handle to connect to a Ceph cluster.
  It encapsulates the RADOS client configuration including username, key
  for authentication, logging, and debugging.

* A handle is created by calling
  `rados_create(rados_t *cluster_handle, const char *const user_id)` or
  `rados_create2(rados_t *cluster_handle, const char *cluster_name,
                 const char *const user_name, uint64_t flags)`.
  When `rados_create` is called, Ceph environment variables are read including
  `$CEPH_ARGS`, which specifies the additional information needed to connect.
  Then, no further configuration of the handle is necessary.
  When `rados_create2` is called, Ceph environment variables are not read and
  there is no need of additional information.
  The `user_id` is a subset of the user_name (For example, the id is 'admin' in
  the name 'client.admin'). `rados_create` assume the username is 'client.'+id.
  It is also possible to create a handle from an existing configuration with
  `rados_create_with_context(rados_t *cluster_handle, rados_config_t cct)`.

* Once created, the handle can be modified using a configuration file,
  environment variables or arguments. The required information is at least one
  monitor address and a path to a Ceph keyring file.

* Once configured, we can connect to the cluster by using the cluster handle
  with `rados_connect(rados_t cluster_handle)`.

* Once connected, the cluster handle can be used to get cluster statistics,
  use pool operations (exists, create, list, delete), read or modify the
  configuration. [1]

### Pool management

* RADOS pools represent separate namespaces for objects. In order to check if a
  pool exists, `rados_pool_lookup(cluster_handle, pool_name)` can be called.
  Its default purpose is to get a pool's id but we can know if a pool does not
  exist by checking if -ENOENT is returned. [2]

* A pool can be created using `rados_pool_create(cluster_handle, pool_name)`.
  The default CRUSH rule is the one of id 0. It is possible to give a specific
  owner id or CRUSH rule when creating a pool. [9], [10]
  It is not possible to change the CRUSH rule of an existent pool.

* Pools can be listed using
  `rados_pool_list(cluster_handle, output_buffer, buffer_size)`
  In the output buffer, each pool is represented with a null-terminated string.
  The end of the list is marked by two 0 bytes.

* A pool can be deleted with `rados_pool_delete(cluster_handle, pool_name)`.
  If a pool cannot be deleted through the API, it is probably because pool
  deletion is not allowed. It can be allowed with the following command:
  `ceph config set mon mon_allow_pool_delete true`.

* Created pools need to be associated with an application before use. It's an
  additional protection for pools to prevent unauthorized types of clients from
  writing data to the pool. If no application is enabled on a pool, a warning
  shows up when checking the cluster status:
  `1 pool(s) do not have an application enabled`.

  In order to associate a pool to an application,
  `rados_application_enable(io_ctx, app_name, force)` can be used when connected
  to the created pool. Default apps names are `cephfs` for the Ceph Filesystem,
  `rbd` for the Ceph Block Device and `rgw` for the Ceph Object Gateway.
  A different name can be specified for a custom application.
  The force parameter is to specify if there is only one application per pool or
  not (value 0 or not).
  [2], [13]

### I/O context

* Once connected to a cluster, a RADOS I/O context is necessary to
  write/read/remove/list/iterate over objects and extended attributes,snapshot
  pools, list snapshots, etc.

* The I/O context is created by calling:
  `rados_ioctx_create(cluster_handle, poolname, &io_context)` [1]

### Media transactions management

* Librados provides synchronous and asynchronous I/O.

* A RADOS operation is **complete** when it is committed on all replicas.
  'safe' is an alias of 'complete'. [18]

* Synchronous calls block and wait for the operation to complete before
  starting the next one.

* Each asynchronous operation needs a `completion` callback of type
  `rados_completion_t` to be executed.
  It represents what shoud be done when the operation is complete.
  A completion is created by calling
  `rados_aio_create_completion2(void *cb_arg, rados_callback_t cb_complete,
                               rados_completion_t *pc)`.
  `cb_complete` is a function to be called when the operation is complete.
  `cb_arg` is the data passed to this function.
  Those parameters can be `NULL` if nothing special has to be done.

* With asynchronous operations, it is possible to ensure a specific operation
  is complete by calling:
  `rados_aio_wait_for_complete(rados_completion_t completion)`.
  In order to wait for the callbacks to be completed as well, we can call:
  `rados_aio_wait_for_complete_and_cb(rados_completion_t c)`.

* For synchronization/flushing, the `rados_aio_flush(io_context)` operation may
  be used. It blocks until all pending writes in an I/O context are complete.
  It doesn't seem to have an impact on cache. It just waits for all requests
  to finish and callbacks to be complete. This function should be called before
  closing the I/O context if asynchronous operations were made.

* The Ceph storage has a cache system named **cache tiering**. By default, there
  is no cache tier. A cache tier can be created by first associating a backing
  storage pool with a cache pool: `ceph osd tier add {storagepool} {cachepool}`.
  Then the cache mode is set with:
  `ceph osd tier cache-mode {cachepool} {cache-mode}`.
  Since the cache tiers overlay the backing storage tier, all client traffic
  must be directed from the storage pool to the cache pool:
  `ceph osd tier set-overlay {storagepool} {cachepool}`. [19]
  An object can be pinned or unpinned in a cache tier. A pinned object won't be
  flushed out to the storage tier. It seems to be the only way to control the
  cache flush, especially if the syncing is made with a different instance of an
  I/O context.
  Since creating a cache tier is only possible through Ceph's command line
  interface, this pin feature would only be interesting for static pools.

* Lock and unlock features on objects are provided by librados. They are not
  needed in the case of Phobos because the Ceph storage is supposed to be used
  through Phobos, which already has those features.

### Object-based interface

* The Ceph storage is an object based storage. Librados directly provides
  write/read/remove operations.

* The write operation is
  `rados_write(io_context, object_name, buffer, buffer_length, byte_offset)`.
  The default max object size is about 128MiB. It can be changed with the
  command `sudo ceph config set osd osd_max_object_size [new size in bytes]`.
  It can also be set in the configuration file before launching the cluster.

* The read operation is
  `rados_read(io_context, object_name, buffer, buffer_length, byte_offset)`.

* The remove operation is `rados_remove(io_context, object_name)`.

* The set operation is
`rados_setxattr(io_context, object_name, attr_name, buffer, buffer_length)`.

* The get operation is
`rados_getxattr(io_context, object_name, attr_name, buffer, buffer_length)`.

* The remove operation on extended attributes is
`rados_rmxattr(io_context, object_name, attr_name)`.

* There is also the operation `rados_getxattrs` to get every attributes of an
  object through an iterator and the operation `rados_getxattrs_next` to get the
  next attribute.

* As stated before, the asynchronous operations need one more argument: the
  'completion' which states what to do when the operation is complete.
  Even when the same 'completion' variable is used for different asynchronous
  operations, those operations seem to work fine at first. But trying to call
  pool operations and listing objects can block or cause a segmentation fault
  when we do that.

* For some reasons, with the C librados library, the read and get operations do
  not give an error when an object does not exist. Nothing happens.

* Write/set operations overwrite objects without warning if they already exist.
  Those two behaviours are not a problem thanks to the DSS.

* During my tests, my cluster was limited to 33 op/s. It is possible to reach
  250,000 op/s if Ceph is well optimized. [17]


### RADOS flags

* RADOS flags can only be used on synchronous operations using object
  operations. An object operation is a macro operation regrouping many atomic
  operations of the same type ("write" type when the operation writes or deletes
  data and "read" when it only reads data).
  This doesn't seem useful in our case.

## Design and implementation

### Address generation

* Information needed to generate an object address for Phobos:
  `extent_key`, `extent_description` (object id). With RADOS, the `address`
  could be the `object_name` in the Ceph storage. The name of a RADOS object
  could simply have the same structure as the address for POSIX I/O adapters.

### Pool implementation

* There are two possibilities to integrate pools to Phobos:

  + A structure `PHO_RADOS_POOL` can be created to represent pools on Phobos.
    A value `DSS_RADOS_POOL` could be added to the `dss_type` enum in the
    pho_dss.h file.
    In order to write/read/delete data on a Ceph storage, new functions might
    have to be implemented in the different APIs to handle the pool parameter.
    A RADOS object would be represented with a Phobos object, A pool field
    would have to be added to the object table as well as a cluster field if we
    have several clusters.

  + The cluster_handle/pool/rados_object structure in RADOS is similar to the
    device/media/object structure in Phobos:

    - As several Phobos nodes could be connected to the same Ceph cluster at the
      same time, clusters would not behave entirely as devices do.
      In order to manage phobos nodes access to pools, a fake device could be
      linked to pools, as already done for dirs.

    - If there is only one Ceph cluster, the necessary information about this
      cluster (the name of the cluster and the location of the Ceph
      configuration file) could be stored directly in the RADOS I/O adapter
      configuration, for example the `pho_rados_open` function or a global
      variable.

    - If there are several clusters or if we want to keep those information in
      the DSS database, the cluster's name may be stored as the `path` field
      in the `device` table. It would be passed to the I/O adapter through the
      `root_path` variable of the `pho_ext_loc`element in the I/O descriptor.
      Every extent has a `media_id`. As the pool/object structure is similar to
      the media/object one, the `media_id` could be the `pool_name` in RADOS's
      case.

* The second option was chosen since it is more in line with the current Phobos
  ressources' structure. It was decided that a Phobos node can only connect to
  one cluster and several Phobos nodes can connect to the same cluster.

* An important factor is the handling of locks on pools: Can several Phobos
  clients access a pool at the same time ? The current ressources used on Phobos
  do not allow this. For the beginning of the implementation this logic will be
  followed.

* Note that it is possible to have different usernames and access to pools can
  be restricted with permissions. But those permissions cannot be managed from
  Librados. Therefore, the notion of owner on pools is difficult to apply.

* Another precision is that each RADOS object would actually correspond to a
  Phobos extent in practice.

### I/O descriptor

* As of now, the I/O descriptor structure has the following elements:
  - I/O flags (enum pho_io_flags iod_flags)
  - The local file descriptor (int fd), useless in RADOS's case
  - The operation size (size_t iod_size)
  - The extent location (struct pho_ext_loc  *iod_loc) which contains the needed
    address (object name in RADOS's case)
  - The In/Out metadata operations bufferstruct (pho_attrs iod_attrs)
  - The IO adapter private context (void *iod_ctx)

* In the 'media transactions management' part, we talked about 'completion'
  callbacks.
  They are very important to call asynchronous operations and we can only
  assign one 'completion' to one operation. Therefore, if we plan to do several
  operations in a row before disconnecting from the cluster, a structure is
  needed to store every 'completion' until they are released.

* Each stored callback would only be used during its initialisation, for the
  operation call and during its release just before freeing the I/O context.
  There is no need to pick an already used completion after the operation call
  it's linked to and there is no need to track which completion corresponds to
  which queued RADOS operation.

* The 'completion' callbacks may be stored in a linked list stored in the I/O
  descriptor. Then each callback would be created and initialized in each
  function used for the operation call, then added at the beginning of that
  list.

* If we plan on using pool operations often or to be able to close the cluster
  connection, it is necessary to have access to the cluster handle. For this
  reason, it is important to store it in the I/O descriptor.

* Since the IO adapter private context is abstract, another solution could be to
  create a structure `rados_io_ctx` holding the cluster, the real RADOS io
  context and a pointer to the list of "completion" callbacks.

### I/O flags

* Current IO flags are:
  - PHO_IO_MD_ONLY (Only operate on object MD)
  - PHO_IO_REPLACE (Replace the entry if it exists): At first glance, RADOS
    seems to automatically replace the entry when it exists. A test has to be
    done to verify it. Then we could check the entry's existence before trying
    to add its new value.
  - PHO_IO_NO_REUSE (Drop file contents from system cache): it doesn't seem
    useful with RADOS unless we use the pin/unpin feature

The first 2 flags can be used with Librados. No additional flag has to be added.

### Interfaces

* get
* delete
* open
* write
* close
* sync

### Calls

* pho_rados_get(const char *extent_key, const char *extent_desc,
                struct pho_io_descr *iod)
* pho_rados_del(struct pho_ext_loc *loc)
* pho_rados_open(const char *extent_key, const char *extent_desc,
                 struct pho_io_descr *iod, bool is_put)
* pho_rados_write(struct pho_io_descr *iod, const void *buf,
                  size_t count)
* pho_rados_close(struct pho_io_descr *iod)
* pho_rados_sync(struct pho_io_descr *iod): The problem with this call is that
  the I/O adapter's sync call is supposed to have a `const char *`
  parameter but only the I/O context is necessary in our case.

## References

[1] : https://docs.ceph.com/en/latest/rados/api/librados-intro/
[2] : https://docs.ceph.com/en/latest/rados/api/librados/
[3] : https://docs.ceph.com/en/latest/rados/operations/data-placement/
[4] : https://docs.ceph.com/en/latest/rados/operations/placement-groups/
[5] : https://docs.ceph.com/en/latest/rados/operations/crush-map/
[6] : https://docs.ceph.com/en/latest/rados/configuration/storage-devices/
[7] : https://docs.ceph.com/en/latest/architecture/#mapping-pgs-to-osds
[8] : https://docs.ceph.com/en/latest/rados/configuration/filestore-config-ref/
[9] : https://docs.ceph.com/en/latest/dev/cache-pool/
[10] : https://docs.ceph.com/en/latest/dev/erasure-coded-pool/
[11] : https://docs.ceph.com/en/latest/start/intro/
[12] : https://docs.ceph.com/en/latest/mgr/orchestrator_modules/
[13] : https://docs.ceph.com/en/latest/rados/operations/pools/
       #associate-pool-to-application
[14] : https://docs.ceph.com/en/latest/install/
[15] : https://docs.ceph.com/en/latest/install/index_manual/#install-manual
[16] : https://docs.ceph.com/projects/ceph-deploy/en/latest/
[17] : https://www3.nd.edu/~dthain/courses/cse40771/spring2007/papers/ceph.pdf

[18] : https://github.com/ceph/ceph/commit/
       46fa68eb10f7deb0798c4bca364636a7d84d1711
[19] : https://github.com/ceph/ceph/blob/
       a67d1cf2a7a4031609a5d37baa01ffdfef80e993/
       doc/rados/operations/cache-tiering.rst
