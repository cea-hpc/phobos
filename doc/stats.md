The document describes the metrics reported by 'phobos stats'.

# Stats from phobosd


 |      Tags      |        Metric Name       |  Type   |       Description                                             |
 |----------------|--------------------------|---------|---------------------------------------------------------------|
 | family, device | dev.nb_mount             | counter | Number of device mounts.                                      |
 | family, device | dev.nb_umount            | counter | Number of device unmounts.                                    |
 | family, device | dev.nb_load              | counter | Number of device loads.                                       |
 | family, device | dev.nb_unload            | counter | Number of device unloads.                                     |
 | family, device | dev.nb_format            | counter | Number of device formats.                                     |
 | family, device | dev.requested_sync       | counter | Number of requested device synchronizations.                  |
 | family, device | dev.effective_sync       | counter | Number of effective device synchronizations.                  |
 | family, device | dev.mount_errors         | counter | Number of device mount errors.                                |
 | family, device | dev.umount_error         | counter | Number of device unmount errors.                              |
 | family, device | dev.load_errors          | counter | Number of device load errors.                                 |
 | family, device | dev.unload_errors        | counter | Number of device unload errors.                               |
 | family, device | dev.format_errors        | counter | Number of device format errors.                               |
 | family, device | dev.sync_errors          | counter | Number of device synchronization errors.                      |
 | family, device | dev.tosync_count         | gauge   | Number of pending synchronizations on the device.             |
 | family, device | dev.tosync_size          | gauge   | Size of pending synchronizations on the device (bytes).       |
 | family, device | dev.total_tosync_size    | counter | Size of data synchronized on the device (bytes).              |
 | family, device | dev.tosync_extents       | gauge   | Number of extents to be synchronized on the device.           |
 | family, device | dev.total_tosync_extents | counter | Number of extents synchronized on the device.                 |
 | family         | sched.incoming_qsize     | gauge   | Size of the incoming task queue for scheduling.               |
 | family         | sched.retry_qsize        | gauge   | Size of the retry task queue for scheduling.                  |
 | family         | sched.ongoing_format     | gauge   | Number of ongoing formats.                                    |
 | request        | req.count                | counter | Number of received requests by type.                          |
 | request        | req.media_requested      | counter | Number of media requested for READ and WRITE operations.      |
 | request        | req.tosync_media_cnt     | counter | Number of released media after a WRITE operation.             |
 | request        | req.tosync_size          | counter | Size of written data to sync after a WRITE operation (bytes). |
 | request        | req.tosync_extents       | counter | Number of extents to sync after a WRITE operation.            |
 | request        | req.nosync_media_cnt     | counter | Number of media released after READ operation.                |
 | request        | req.nosync_size          | counter | Size of data released after READ operation (bytes).           |
 |                | req.response_qsize       | gauge   | Size of the global response queue.                            |

