[ltfs] - Configuring LTFS (Linear Tape File System)
===================================================

This section explains how to use LTFS
(https://github.com/LinearTapeFileSystem/ltfs) within Phobos. All LTFS-related
parameters should be listed under the **[ltfs]** section. Each parameter
corresponds to a command that Phobos will execute, typically invoking an
external program. Phobos includes an internal wrapper **pho_ldm_helper** to
handle LTFS commands execution.

*cmd_parameters*
----------------
The **cmd_format**, **cmd_mount**, **cmd_release**, and **cmd_umount**
parameters specify the commands used to perform operations on LTFS volumes:
formatting, mounting, releasing SCSI reservations, and unmounting respectively.
Each parameter accepts any valid shell command. When using Phobos' internal
wrapper, the value should include the wrapper (/usr/sbin/pho_ldm_helper)
followed by the specific operation and its arguments.

If not specified, Phobos defaults to the following:

* **cmd_format: /usr/sbin/pho_ldm_helper format_ltfs %s %s**

* **cmd_mount: /usr/sbin/pho_ldm_helper mount_ltfs %s %s**

* **cmd_release: /usr/sbin/pho_ldm_helper release_ltfs %s**

* **cmd_umount: /usr/sbin/pho_ldm_helper umount_ltfs %s %s**

For **cmd_format**, the first %s refers to the device to use, and the second to
the tape to format. For **cmd_mount** and **cmd_umount**, the first %s
corresponds to the device to use, and the second to the mount path. For
**cmd_release**, %s corresponds to the device to release.

Example:

.. code:: ini

    [lib_scsi]
    cmd_format  = /usr/sbin/pho_ldm_helper format_ltfs %s %s
    cmd_mount   = /usr/sbin/pho_ldm_helper mount_ltfs %s %s
    cmd_release = /usr/sbin/pho_ldm_helper release_ltfs %s
    cmd_umount  = /usr/sbin/pho_ldm_helper umount_ltfs %s %s
