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

To rectify this behaviour, we propose a better mangement of the SCSI errors, in
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
 - define the actions to follow after a resource error.

## Resource health

The first step proposed is to associate each resource with an health counter.
When the resource fails for any SCSI-related reason, we decrease its health
counter, and when an operation is successful, it is increased. As such, we will
require adding this health counter to each resource in the DSS, meaning a new
column for the database for each of the "device" and "medium" tables.

When this counter goes down (i.e. a SCSI error is encountered), the following
actions will be taken:
 - interrupt the ongoing operation,
 - try to unmount/unload the tape currently on the drive (if this operation
 fails, we will have to preemptively mark the tape as failed and notice an
 administrator that manual intervention may be required),
 - reschedule the operation that was ongoing (if any) so that it may be
 completed later.

Morever, during Phobos' uptime, this counter may at some point become 0, in
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

## Error logging

While accounting for SCSI errors with this health counter, it is also important
to know and understand the errors that arose, and the ones that may arise. To
this end, we will add a logging mechanism for these errors. To ensure their
readibility and longevity, we will not use the current logging system used by
Phobos but instead use new DSS table, with the following schema:

```sql
TABLE error_logs(
    device    varchar(2048) UNIQUE,
    medium    varchar(2048) UNIQUE,
    uuid      varchar(36) UNIQUE DEFAULT uuid_generate_v4(),
    errno     integer NOT NULL,
    cause     varchar (256),
    msg       varchar(2048),
    time      timestamp DEFAULT now(),

    PRIMARY KEY (uuid)
);
```

In this schema:
 - the "device" corresponds to the device id that failed (primary key of the
   "device" table),
 - the "medium" corresponds to the medium id that failed (primary key of the
   "medium" table),
 - the "uuid" is the primary key of the logs table,
 - the "errno" is the error number of the log (must be different from 0),
 - the "cause" is the action that triggered the log,
 - the "msg" is additionnal information about the error (for instance an error
 message returned by the library),
 - and "time" is the timestamp at which time the log was produced.

Regarding the "errno", we plan on improving our use of the errno variable in
Phobos to add different error codes pertaining to the internal logic of Phobos
or some specific errors.

Moreover, when a tape/drive encounters a SCSI error, we will try to reschedule
the operation if possible (as stated above in the 'resource health' section). As
such, we should prevent reusing the same tape and drive for that operation, as
they are more likely to trigger the same error, and we would not be able to gain
further information about which component is causing the error.

Therefore, when scheduling resources for operation, before selecting a
tape/drive and indicating them back to the user, the daemon should also check
that they did not cause an error recently (the usage of 'recently' is
voluntarily loose here, check the 'Additionnal remarks and thoughts' for more
information). If they did, we should try to switch either the tape or the drive
used, depending on the operation.

To manage these information, we propose a new structure containing these info:

```c
struct pho_error_log {
    struct pho_id device;
    struct pho_id medium;
    int error_number;
    char *cause;
    char *message;
    timestamp_t time;
};
```

This structure will correspond to the log to record and be inserted in the DSS.
To filter the logs, we propose another structure:

```c
struct pho_error_log_filter {
    struct pho_id device;
    struct pho_id medium;
    int *error_number;
    timestamp_t *start_time;
    timestamp_t *end_time;
};
```

Here, each field will be used, if not NULL, to filter through every logs, and
return only the ones that satisfy the given criteria.

## DSS functions

### Health counter

To use and act on this health counter at the DSS level, a few structures and
the behaviour of some functions will have to be modified. Namely:
 - the structures `media_info` and `dev_info` will have to be extended to
 contain the health counter,
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
 * @param[in]   log            error log to emit
 *
 * @return 0 if success, -errno if an error occurs
 */
int dss_emit_log(struct dss_handle *dss, struct pho_error_log *log);

/**
 * Dump a list of logs to a given file.
 *
 * If any field of \p log_filter is non-NULL, it will be used to filter the
 * entries to dump.
 *
 * @param[in]   dss            DSS to request
 * @param[in]   log_filter     filter for the error logs to dump
 * @param[out]  logs           error logs retrieved after filtering
 * @param[out]  n_logs         number of error logs error logs retrieved
 *
 * @return 0 if success, -errno if an error occurs
 */
int dss_dump_logs(struct dss_handle *dss,
                  struct pho_error_log_filter *log_filter,
                  struct pho_error_log **logs, int *n_logs);

/**
 * Clear a list of logs.
 *
 * If any field of \p log_filter is non-NULL, it will be used to filter the
 * entries to clear.
 *
 * @param[in]   dss            DSS to request
 * @param[in]   log_filter     filter for the error logs to clear
 *
 * @return 0 if success, -errno if an error occurs
 */
int dss_clear_logs(struct dss_handle *dss,
                   struct pho_error_log_filter *log_filter);
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
 * @param[in]   fd             file descriptor where the logs should be dumped
 * @param[in]   log_filter     filter for the error logs to clear
 *
 * @return 0 if success, -errno if an error occurs
 */
int phobos_admin_dump_logs(struct admin_handle *adm, FILE *file,
                           struct pho_error_log_filter *log);

/**
 * Clear a list of logs.
 *
 * If any field of \p log_filter is non-NULL, it will be used to filter the
 * entries to clear.
 *
 * @param[in]   adm            Admin module handler.
 * @param[in]   log_filter     filter for the error logs to clear
 * @param[in]   clear_all      true to clear every log, cannot be used if any
 *                             of the other parameters except \p adm is given
 *
 * @return 0 if success, -errno if an error occurs
 */
int phobos_admin_clear_logs(struct admin_handle *adm,
                            struct pho_error_log_filter *log);
```

## CLI functions

Alongside the admin API functions, we propose 2 similar CLI commands:

```
usage: phobos logs dump [-h] [--device ID] [--medium ID] [-e ERRNO]
                        [--start TIMESTAMP] [--end TIMESTAMP] [-f path]

positional arguments:
  path                  file where the logs should be dumped to

optional arguments:
  -h, --help            show this help message and exit
  --device              device ID of the logs to dump
  --medium              medium ID of the logs to dump
  -e, --errno           error number of the logs that should be dumped
  --start               timestamp before which the logs that should be dumped,
                        in format YYYY-MM-DD hh:mm:ss
  --end                 timestamp after which the logs that should be dumped,
                        in format YYYY-MM-DD hh:mm:ss
  -f, --file            the path of where to dump the logs, if not given, will
                        dump to stdout

usage: phobos logs clear [-h] [--device ID] [--medium ID] [-e ERRNO]
                         [--start TIMESTAMP] [--end TIMESTAMP] [--all]

optional arguments:
  -h, --help            show this help message and exit
  --device              device ID of the logs to clear
  --medium              medium ID of the logs to clear
  -e, --errno           error number of the logs that should be dumped
  --start               timestamp before which the logs that should be dumped,
                        in format YYYY-MM-DD hh:mm:ss
  --end                 timestamp after which the logs that should be dumped,
                        in format YYYY-MM-DD hh:mm:ss
  --all                 must be specified to clear all logs, will have no effect
                        if any of the other arguments is specified
```

## Additionnal remarks and thoughts

Regarding the time window during which a tape/drive couple may not be reschedule
together after an SCSI error, we have two choices: either considered this to
only pertain to the operation that triggered this error, meaning another
operation 2 minutes later may use the same couple, or add a new parameter to the
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
errors. For instance, errors that occured more than a month ago should not be
counted anymore to calculate the health counter of a resource.
