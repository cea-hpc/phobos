[admin] - Configuring the admin operations
==========================================

This section explains how to configure the admin operations of Phobos,
like the media addition. All admin-related parameters should be listed
under the **[admin]** section.

*default_ranking*
-----------------

The **default_ranking** parameter defines the ranking of the media added
with no explicit ranking, for each media family. The value is a
comma-separated list of <family>=<mode> entries, with the mode either
"stationary" or "growing".

With "stationary", the new media get the highest ranking value currently
in the DSS for their family. With "growing", they get this value plus
one. As the lowest ranking is the preferred one, the media added with
the default ranking are the least preferred of their family. A single
default ranking is computed for all the media of a single addition,
before they are inserted.

If this parameter is not specified, Phobos defaults to the following:
**default_ranking = tape=stationary,dir=stationary,rados_pool=stationary**.

Example:

.. code:: ini

    [admin]
    default_ranking = tape=growing,dir=stationary,rados_pool=stationary
