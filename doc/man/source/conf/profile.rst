Creating profiles in Phobos
===========================

This section explains how to create a profile in Phobos. Within a profile, you
can define a preset of parameters for the "phobos put" command. Multiple
profiles can be created for different use cases. To define a profile, a new
section must be added to the configuration file using the following format:
**[profile %s]**. Here, "%s" should be replaced with the profile name. This name
is used when specifying a profile in the put command.

*Profile behavior*
------------------

* If a profile is used alongside other parameters in the put command, the
  explicitly specified values will override those in the profile.
* If additional tags are provided, they will be merged with the tags from the
  profile.

*Available options*
-------------------

* family :     the storage family to use
* layout :     the layout configuration
* library :    the library to use
* lyt-params : parameters associated with the layout (should be a list of
               key/value pairs)
* tags :       tags to use (should be a list)

Example:

.. code:: ini

    [profile "simple"]
    layout = raid1
    lyt-params = repl_count=1,check_hash=true
    library = legacy
