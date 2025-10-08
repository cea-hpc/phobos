====================
Phobos Configuration
====================

This document provides a detailed explanation of the structure and sections
within the configuration file of Phobos. To configure Phobos, you can edit the
configuration file, which is by default located at **/etc/phobos.conf**. This
file is used by both the client for performing operations and by the daemons
(Phobosd and TLC) to control their behavior.

General structure
=================

The configuration file is divided into multiple sections, each beginning with a
header enclosed in square brackets '[]'. Each section contains key-value pairs
that define configuration parameters for different components of Phobos.

Example section:

.. code:: sh

    [section_name]
    key1 = value1
    key2 = value2

Different sections in configuration file
========================================

Below are listed, all the different sections available in the configuration file
of Phobos.

.. include:: conf/copy.rst

.. include:: conf/dss.rst

.. include:: conf/family.rst

.. include:: conf/io.rst

.. include:: conf/iosched.rst

.. include:: conf/layout.rst

.. include:: conf/lrs.rst

.. include:: conf/ltfs.rst

.. include:: conf/profile.rst

.. include:: conf/scsi.rst

.. include:: conf/store.rst

.. include:: conf/tape.rst

.. include:: conf/tlc.rst

.. include:: conf/hsm.rst

Alternative configuration specification
=======================================

While you can modify the configuration file and restart the daemon, you can also
specify each of these parameters by using environment variables. The variables
should be of the form **PHOBOS_<section in capital>_<parameter key> = <parameter
value>**.

For instance, you can export the following variables:

.. code:: sh

    export PHOBOS_LRS_families="dir"
    export PHOBOS_STORE_default_family="dir"
    export PHOBOS_LTFS_cmd_mount="sh -c 'exit 1'"

Finally, even if you do not specify every parameter defined in the configuration
file, Phobos can still operate, as Phobos has default values for these
parameters.

Warning
=======

Sections that have a key and a string as the header, such as `[key "string"]`
(e.g., profiles or TLC), cannot be configured using environment variables.
