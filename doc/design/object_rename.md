# Rename an object

## Calling the function
### Using the API-C
One can access data in phobos using the phobos API:

From `store.c`, call for renaming objects is: ```c
int phobos_rename(
    char *old_oid,  // Current object ID to be replaced.
    char *uuid,     // UUID of the object.
    char *new_oid   // Object ID to be given to the object.
)
```
One of the parameters `old_oid` or `uuid` should be `NULL`.

### Using the CLI
The CLI wraps `phobos_rename()` calls.
```bash
# phobos rename --oid <old_oid> <new_oid>
phobos rename --oid my_good_old_name my_whole_new_oid

# phobos rename --uuid <uuid> <new_oid>
phobos rename --uuid 123456-789-abcdef "here's another new oid"
```

## Description
This function changes the __oid__ of an object to `new_oid`, among all the
versions associated with its generation, in both ``object`` and
``deprecated_object`` tables.

If the existing object is identified using its __oid__, its last version must
be alive.

Otherwise, if the existing object is identified using its __uuid__, its latest
version can be either alive or deprecated (i.e. the object has been deleted).

## Security
Once all entries to rename are found, the rename must be done atomically.
Either the object is not renamed if the OID already exists or was reserved by
someone else, or the rename is successful.