# Phobos admin interface in Python

Command name: phobos

## Directories
Directories are considered as both devices and media.
Their support is useful for testing, so they should be implemented first.

* phobos dir add [--unlock] <dirs(s)>
  * query directory information through ldm_device_query()
  * insert a 'dir' device in locked state using dss_device_insert()
  * query directory information through ldm_media_query()
  * insert a 'dir' media in locked state using dss_media_set()
  * if --unlock is specified, unlock the device and media
    using dss_{device,media}_set()
* phobos dir {list|ls}
  * list directories using dss_{device,media}_get()
* phobos dir show <dir(s)>
  * show device/media information using dss_{device,media}_get()
* phobos dir lock/unlock
  * set device and media locking status using dss_{device,media}_set().
* phobos dir update {-T|--tags} <tag(s)> <dir(s)>
  * set a new tags list for this dir medium. Old tags are erased by this new
    list.

## Drive
* phobos drive add </dev/foo> [--unlock]
  * get device information using ldm_device_query()
  * insert it to device table in locked state (unless --unlock is specified),
    using dss_device_insert()
* phobos drive {list|ls} [--model model]
  * use dss_device_get()
* phobos drive show <devpath|drv_id>
  * use dss_device_get()
* phobos drive lock|unlock <devpath|drv_id>
  * use dss_device_update_adm_status()

## Tape
* phobos tape add --type ltoX [--fs <fstype>] <label(s)>
  * add tapes to DSS in locked mode using dss_media_set()
    * labels syntax relies on 'nodeset'
    * --type parameter is mandatory
    * if fstype is not specified, use default returned by pho_cfg_get()
* phobos tape format <label(s)> [--nb_streams <nb_streams>] --unlock
  * format tapes to the given fstype:
    * call lrs_format() -> load the tape in a drive and format the tape
    * if specified, multiple format operations can be started in parallel
      (up to <nb_streams>)
* phobos tape show <label(s)>
  * use dss_media_get()
* phobos tape {list|ls} [-t type]
  * use dss_media_get()
* phobos tape {lock|unlock}
  * use dss_media_set()
* phobos tape update {-T|--tags} <tag(s)> <dir(s)>
  * set a new tags list for this tape medium. Old tags are erased by this new
    list.

## Object store
* phobos get [--uuid u] [--version v] <object_id> <dest_file>
  * Retrieve an item from the object store
    * no bulk operation supported yet
    * If neither --uuid nor --version are specified, we can only get alive
      objects.
    * If --uuid is specified but not --version, get the most recent object.
    * If --version is specified but not --uuid, get the current generation of
      object.
    * If both --uuid and --version are specified, get the object matching the
      2 criteria and given oid.
* phobos put [--family f] [--metadata k=v,...] [--overwrite] [--tags a,b,...]
             [--layout l] [--profile p] <orig_file> <object_id>
  * Insert an item into the object store using the given family
    * If --family is specified, only the media of this specified family
      will be considered when putting the object.
    * If --metadata is specified, attributes are parsed as a comma-separated
      list of key=value pairs. Quotes can be used to encapsulate separators
      within the keys or values.
      eg: owner=bob,layout="ost0,ost1,ost2",attr_scheme="k=v"
    * If --overwrite is specified, the object will be overwritten if already
      present in the DSS, and the new version will have its version_number
      incremented. If not, the object will be inserted.
    * If --tags is specified, only the media containing the given tags will
      be considered when trying to put.
      eg: fast,sp -> only the media containing the tags "fast" and "sp" will be
      used
    * If --profile is specified, the parameters defined in the configuration file
      (usually "/etc/phobos.conf") will be used. These parameters can be
      the family, the layout or tags. They will be used if not already
      specified through the command line (eg: if you put with "--profile p
      --layout b", and if profile 'p' defines a specific layout, then that
      layout will not be used, as 'b' takes priority).
* phobos mput [--family f] [--tags a,b,...] [--layout l] [--alias a]
              <input_file>
  * Bulk insert multiple items into the object store.
    * file can be "-" to make phobos read from stdin.
    * the input format is expected to be:
      * optional comments
        <orig_file>   <object_id>  <metadata|->
    * use "-" for empty metadata. See 'phobos put' section of this document
      for metadata formatting.
    * All the specified options behave the same way as specified in
      'phobos put', so refer to this section for more information.
