Configuring the tape/drive support and compatibility rules
==========================================================

This sections explains how to configure tape and drive support and defines
compatibility rules between tapes and drives in Phobos.

*Configuring supported drive models for a generation of drive*
--------------------------------------------------------------

This section explains how to add a list of supported models for a specific
generation of drives. To define the supported models, a new section must be
added to the configuration file using the following format: **[drive_type %s]**.
Here, "%s" should be replaced with the generation of the targeted drive (e.g.,
"LTO5_drive"). There is only one parameter to configure.

*models*
~~~~~~~~

The **models** parameter defines all the drive models supported by a particular
generation of drives in Phobos. The value must be a comma-separated list without
any spaces.

If this parameter is not specified, Phobos will be unable to use this
generation of drives, as it will not be able to check compatibility with tapes.

Example:

.. code:: ini

    [drive_type "LTO6_drive"]
    models = ULTRIUM-TD6,ULT3580-TD6,ULTRIUM-HH6,ULT3580-HH6

*Configuring supported tape models*
-----------------------------------

The parameters used to configure the supported tape models by Phobos should be
listed under the **[tape_model]** section.

*supported_list*
~~~~~~~~~~~~~~~~

The **supported_list** parameter defines all the tape models that Phobos can
support. Phobos checks this list when adding a new tape ensure compatibility.
The value must be a comma-separated list without spaces.

If this parameter is not specified, Phobos defaults to the following:
**supported_list = LTO5,LTO6,LTO7,LTO8,LTO9,T10KB,T10KC,T10KD**

Example:

.. code:: ini

    [tape_model]
    supported_list = LTO5,LTO6,LTO7,LTO8,LTO9,T10KB,T10KC,T10KD

*Creating compatibility rules between tapes and drives*
-------------------------------------------------------

This section explains how to create compatibility rules between tapes and
drives in Phobos. To define a new rule, a new section must be added to the
configuration file using the following format: **[tape_type %s]**. Here, "%s"
should be replaced with the technology of the targeted tape (e.g., "LTO5").
Currently, there is only one parameter to configure when adding a new rule.

*drive_rw*
~~~~~~~~~~

The **drive_rw** parameter defines the type of drives that can be used with a
specific technology or generation of tapes. The values specified should
correspond to the drive models specified as section above.

Its value must be a comma-separated list without space.

If this parameter is not specified, Phobos will be unable to use this
generation of drives, as it will not be able to check compatibility with tapes.

Example:

.. code:: ini

    [tape_type "LTO6"]
    drive_rw = LTO6_drive,LTO7_drive
