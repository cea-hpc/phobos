# SCSI error management

Currently, when an SCSI error is encountered while manipulating a tape/drive
couple, we consider the error pertains to both of them. As such, we set their
status in the DSS as failed. That means this tape and this drive will not be
used again for the rest of Phobos' uptime or until they are put back online by
an administrator.

This means that in case only one of the two is faulty, we will still consider
both of them to be failed, and without external input (for instance an admin
resetting tape or drive status in the database), a valid resource cannot be used
again. This can be very problematic especially regarding the tape, as all data
cannot be retrieved if marked as failed in the DSS.

To rectify this behaviour, we propose a better management of the SCSI errors, in
order to more easily target actually failed resources, while also handling
general failures of the library better.

## Goals of this document

This document and the different mechanisms explained aim to answer specific
goals, hereby listed:

- maintain an history of errors that occurred (SCSI here, but may be extended
  to LTFS, I/O client issues, DSS, ...) and the resources implicated,
- evaluate the number of errors a medium or device encountered within a certain
  time frame,
- prevent the usage of a medium/device couple that errored recently,
- devise strategies to find the root of an error (medium, device, library,
  ...),
- ensure the longevity of these information and their resilience to crashes,
- define the actions to follow after a resource error,
- provide an history of operations done by a drive.

## Resource health

The first step proposed is to associate each resource with an health counter.
When the resource fails for any SCSI-related reason, we decrease its health
counter, and when an operation is successful, it is increased. As such, we will
require adding this health counter to each resource in the DSS, meaning a new
column for the database for each of the "device" and "medium" tables.

When this counter goes down (i.e. an SCSI error is encountered), the following
actions will be taken:

- interrupt the ongoing operation,
- try to unmount/unload the tape currently on the drive (if this operation
  fails, we will have to preemptively mark the tape as failed and notice an
  administrator that manual intervention may be required. The associated drive
  will not be useable as well),
- reschedule the operation that was ongoing (if any) so that it may be
  completed later.

Moreover, during Phobos' uptime, this counter may at some point become 0, in
which case the resource will be marked as failed, and specific actions will be
taken. In the case of a drive, we will:

- execute the general "health decrease" operations specified above,
- mark the drive in the DSS as failed so it may not be picked again for
  rescheduling,
- remove this device from the list of usable devices for its host,
- reschedule any operation pending on this drive,
- forcefully interrupt the corresponding device thread.

In the case of a tape, the following actions will be taken:
- execute the general "health decrease" operations specified above,
- mark the tape in the DSS as failed,
- reschedule any operation pending on this tape if possible (a failed tape may
  prevent the retrieval of an object, a format will always fail, ...).

The administrator should be able to reset the health of a device. Indeed, when a
physical drive is changed in a library, the new one has the same serial number.
Therefore, its health should be reset. Also, the previous errors encountered
should not be considered when choosing a device/medium pair.

This should also be done for tapes. It happens that an unreadable tape becomes
usable again after a reformat. Therefore, the administrator should be able to
reset the health counter after a format.

Both for tapes and drives that need to be set back into service, the
administrator will be expected to clear and possibly dump the logs associated to
restore its full health and unlock the resource. Otherwise, it will still be
considered failed by Phobos.

## Configuration file

To implement this resource health mechanism, the configuration file will be
extended to add 4 parameters:
 - the initial health counter for tapes;
 - the maximum health counter for tapes;
 - the initial health counter for drives;
 - the maximum health counter for drives.

These values are specified here only for tapes and drives, but can be extended
to other types of media and devices, though we will stay specific to SCSI in
this document.

The initial health counter corresponds to the value set up in the DSS when a
resource is added/loaded.

Having a maximum health counter is required to efficiently detect failures,
because since we will increase it when an operation is successful, a resource
may attain a large health count, and failing a dozen times will not be enough to
set that health count to 0.

These parameters may be extended and specialized for different models and/or
generations of resources, but we will have to investigate if it is necessary
and/or useful.

## Logging

While accounting for SCSI errors with this health counter, it is also important
to know and understand the errors that arose, and the ones that may arise. To
this end, we will add a logging mechanism for these errors. To ensure their
readability and longevity, we will not use the current logging system used by
Phobos but instead use new DSS table, with the following schema:

```sql
TYPE operation_type AS ENUM ('library_scan', 'library_open', 'device_lookup',
                             'medium_lookup', 'device_load', 'device_unload');

TABLE logs(
    family    dev_family
    device    varchar(2048),
    medium    varchar(2048),
    uuid      varchar(36) UNIQUE DEFAULT uuid_generate_v4(),
    errno     integer NOT NULL,
    cause     operation_type,
    message   jsonb,
    time      timestamp DEFAULT now(),

    PRIMARY KEY (uuid)
);
```

In this schema:

- the "family" corresponds to the family of device/medium this log is about,
- the "device" corresponds to the device id that failed (primary key of the
  "device" table),
- the "medium" corresponds to the medium id that failed (primary key of the
  "medium" table),
- the "uuid" is the primary key of the logs table,
- the "errno" is the error number of the log,
- the "cause" is the Phobos action that triggered the log (for instance
  loading a medium into a device),
- the "msg" is additional information about the error (for instance an error
  message returned by the library),
- and "time" is the timestamp at which time the log was produced.

Regarding the "errno", we plan on improving our use of the errno variable in
Phobos to add different error codes pertaining to the internal logic of Phobos
or some specific errors.

Moreover, when a tape/drive encounters an SCSI error, we will try to reschedule
the operation if possible (as stated above in the 'resource health' section). As
such, we should prevent reusing the same tape and drive for that operation, as
they are more likely to trigger the same error, and we would not be able to gain
further information about which component is causing the error.

Therefore, when scheduling resources for an operation, before selecting a
tape/drive and indicating them back to the user, the daemon should also check
that they did not cause an error recently (the usage of 'recently' is
voluntarily loose here, check the 'Additional remarks and thoughts' for more
information). If they did, we should try to switch either the tape or the drive
used, depending on the operation.

These logs will include the history of successful operations (load and unload)
done on a drive with the tape that was loaded. This information is valuable
because it happens that a faulty drive writes data on tapes in such a way that
only that drive can read them back. In such a situation, it is important to get
the whole history of load and unload operations done by this drive to know which
tapes are affected. This log should be kept as long as the library is in use
because such errors can be detected long after the tape have been written.

To manage these information, we propose a new structure containing these info:

```c
enum operation_type {
    PHO_LIBRARY_SCAN,
    PHO_LIBRARY_OPEN,
    PHO_DEVICE_LOOKUP,
    PHO_MEDIUM_LOOKUP,
    PHO_DEVICE_LOAD,
    PHO_DEVICE_UNLOAD,
};

struct pho_log {
    struct pho_id device;
    struct pho_id medium;
    int error_number; /* 0 if the operation was a success */
    enum operation_type cause;
    json_t *message;
    struct timeval time;
};
```

This structure will correspond to the log to record and be inserted in the DSS.
To filter the logs, we propose another structure:

```c
struct pho_log_filter {
    struct pho_id device;
    struct pho_id medium;
    int *error_number;
    enum operation_type cause;
    struct timeval *start_time;
    struct timeval *end_time;
};
```

Here, each field will be used, if not NULL, to filter through every logs, and
return only the ones that satisfy the given criteria.

### Low-level recording

To log the different operations, we will stay at the Phobos LRS level, meaning
only the functions that will call upon the LDM modules will log their actions.
As such, the device, medium and cause are available before the call to the LDM,
while the error number is available after it. That means the only thing the LDM
layer has to fill is the message, as different JSON entries.

Therefore, we only need to modify the LDM so that each operation takes in a
`json_t` and fills it with what it deems necessary for the operation. For
instance, a move from a slot to a driver will record the source address of the
tape and the target address, the low-level SCSI operation (`MOVE_MEDIUM`) and
the potential SCSI error as returned by the library.

### Operations to record

Following is a list of the different operations done by the LRS that can be
recorded. However, they are only recorded if they fail, except for the load and
unload operations, that will also be recorded on success:
 - Library open: the library status done at open,
 - Drive lookup: the operation to lookup a drive, retrieve information about it
 and whether it contains a tape or not,
 - Medium lookup: the operation to lookup a medium, retrieve information about
 it and its address,
 - Device load: the operation to load a medium into a device, with two known
 addresses,
 - Device unload: the operation to unload a medium from a device, with the
 source address known but the destination left up to the LDM,

Here are the operations done on the library from other Phobos components:
 - Library scan: the operation to scan the library and get all the information
 about it. Used by the admin module.

## DSS functions

### Health counter

To use and act on this health counter at the DSS level, a few structures and
the behaviour of some functions will have to be modified. Namely:

- the structure `media_stats` will have to be extended to contain
  the health counter,
- the health counter will have to be accessible for devices as well, but
  there is no "dev\_stats" structure. We must either add the health to
  the `dev_info` structure or create a `dev_stats`;
- `dss_media_from_pg_row` and `dss_device_from_pg_row` will be updated to
  retrieve the health counter and put it into the above structures,
- the function `dss_device_set` will have to be implemented, which will be a
  wrapper of `dss_generic_set`, as it already handles devices.
- the functions `get_device_setrequest` and `get_media_setrequest` will be
  modified to construct a DB request including this health counter, and also, if
  that health counter is 0, mark the resource as failed. This is a simple
  optimisation to prevent doing two DSS requests instead of one.

### Logging mechanism

To manage the logs table, we propose 3 new functions:

```c
/**
 * Emit a log, all fields of \p log must be filled except for 'time' which will
 * be considered as 'now()'.
 *
 * @param[in]   dss            DSS to request
 * @param[in]   log            log to emit
 *
 * @return 0 if success, -errno if an error occurs
 */
int dss_emit_log(struct dss_handle *dss, struct pho_log *log);

/**
 * Dump a list of logs to a given file.
 *
 * If any field of \p log_filter is non-NULL, it will be used to filter the
 * entries to dump.
 *
 * @param[in]   dss            DSS to request
 * @param[in]   log_filter     filter for the logs to dump
 * @param[out]  logs           logs retrieved after filtering
 * @param[out]  n_logs         number of error logs error logs retrieved
 *
 * @return 0 if success, -errno if an error occurs
 */
int dss_dump_logs(struct dss_handle *dss,
                  struct pho_log_filter *log_filter,
                  struct pho_log **logs, size_t *n_logs);

/**
 * Clear a list of logs.
 *
 * If any field of \p log_filter is non-NULL, it will be used to filter the
 * entries to clear.
 *
 * @param[in]   dss            DSS to request
 * @param[in]   log_filter     filter for the logs to clear
 *
 * @return 0 if success, -errno if an error occurs
 */
int dss_clear_logs(struct dss_handle *dss,
                   struct pho_log_filter *log_filter);
```

## API functions

While the health counter will stay an internal variable that cannot be acted
upon by a user or admin (outside of the configuration file), the logs function
can, except to emit a log. As such, we provide 2 API functions to dump and clear
logs:

```c
/**
 * Dump a list of logs to a given file.
 *
 * If any field of \p log_filter is non-NULL, it will be used to filter the
 * entries to dump.
 *
 * @param[in]   adm            Admin module handler.
 * @param[in]   file           File descriptor where the logs should be dumped.
 * @param[in]   log_filter     Filter for the logs to clear.
 *
 * @return 0 if success, -errno if an error occurs
 */
int phobos_admin_dump_logs(struct admin_handle *adm, int fd,
                           struct pho_log_filter *log);

/**
 * Clear a list of logs.
 *
 * If any field of \p log_filter is non-NULL, it will be used to filter the
 * entries to clear.
 *
 * @param[in]   adm            Admin module handler.
 * @param[in]   log_filter     Filter for the logs to clear.
 * @param[in]   clear_all      True to clear every log, ignored if any of the
 *                             other parameters except \p adm is given
 *
 * @return 0 if success, -errno if an error occurs
 */
int phobos_admin_clear_logs(struct admin_handle *adm,
                            struct pho_log_filter *log,
                            bool clear_all);
```

## CLI functions

Alongside the admin API functions, we propose 2 similar CLI commands:

```
usage: phobos logs dump [-h] [--device ID] [--medium ID] [-e ERRNO]
                        [--start TIMESTAMP] [--end TIMESTAMP] [-f path]

optional arguments:
  -h, --help            show this help message and exit
  -D, --device          device ID of the logs to dump
  -M, --medium          medium ID of the logs to dump
  -e, --errno           error number of the logs that should be dumped
  --start               timestamp of the oldest logs to dump,
                        in format YYYY-MM-DD hh:mm:ss
  --end                 timestamp of the most recent logs to dump,
                        in format YYYY-MM-DD hh:mm:ss
  -f, --file            the path where to dump the logs, if not given, will
                        dump to stdout

usage: phobos logs clear [-h] [--device ID] [--medium ID] [-e ERRNO]
                         [--start TIMESTAMP] [--end TIMESTAMP] [--all]

optional arguments:
  -h, --help            show this help message and exit
  -D, --device          device ID of the logs to clear
  -M, --medium          medium ID of the logs to clear
  -e, --errno           error number of the logs that should be dumped
  --start               timestamp of the oldest logs to dump,
                        in format YYYY-MM-DD hh:mm:ss
  --end                 timestamp of the most recent logs to dump,
                        in format YYYY-MM-DD hh:mm:ss
  --all                 must be specified to clear all logs, will have no effect
                        if any of the other arguments is specified
```

## Additional remarks and thoughts

Regarding the time window during which a tape/drive couple may not be reschedule
together after an SCSI error, we have two choices: either considered this to
only pertain to the operation that triggered this error, meaning another
operation later may use the same couple, or add a new parameter to the
configuration file, which will specify this time window. As a first
implementation solution, the former is easier and will yield results faster, but
the necessity of the second solution will have to be discussed.

Another aspect of having this health counter is for the scheduling: when
checking which tape and drive to use, we can take into account this health
counter, in order to prioritize tapes/drives that are in better health. However,
we do not intend to implement this directly alongside the health counter and
logs, but it is a future improvement.

For the health counter, while it is explained and developed in this document, it
may not be necessary at all, because its usage is tantamount to checking the
number of error logs that concern specific resources and doing the difference
between that number and the initial health counter set in the configuration
file. With this behaviour, we would not increase the health counter when an
operation succeeds, but instead use a time window in which we should count the
errors. For instance, errors that occurred more than a month ago should not be
counted anymore to calculate the health counter of a resource.
