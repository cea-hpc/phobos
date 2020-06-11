% DSS API

# General purpose
This document will address the design discussions around the phobos Distributed
State Service (DSS).

As discussed for now, our goal is to design the DSS as an API which provides
simple commands such as GET, SET and LOCK.

# Generic LOCK
The generic lock calls allow the user to notify they want to do some critical
manipulations that require an exclusive access to data, resources, etc.

The lock identifier must be unique and can be based on whatever is needed,
such as a (type, name) pair if locking a resource.

## Lock call
This call registers a new entry for the given lock. If the specified lock is
already registered, the call fails.

This call need to be atomic to prevent two callers registering the same lock at
the same time. If it happens, the 'first' call will succeed, the 'second' one
will fail.

```c
/**
 * Take a lock.
 *
 * @param[in]   handle          DSS handle.
 * @param[in]   lock_id         Lock identifier.
 * @param[in]   lock_owner      Name of the lock owner.
 * @return                      0 on success,
 *                             -EEXIST on lock failure.
 */
int dss_lock(struct dss_handle *handle, const char *lock_id,
             const char *lock_owner);
```

## Refresh call
This call updates the timestamp of the entry referenced by the given lock. If
the specified lock does not exist or is not possessed by the given owner,
the call fails.

```c
/**
 * Refresh a lock timestamp.
 *
 * @param[in]   handle          DSS handle.
 * @param[in]   lock_id         Lock identifier.
 * @param[in]   lock_owner      Name of the lock owner.
 * @return                      0 on success,
 *                             -ENOLOCK if the lock does not exist,
 *                             -EACCESS if the lock owner does not match.
 */
int dss_lock_refresh(struct dss_handle *handle, const char *lock_id,
                     const char *lock_owner)
```

## Unlock call
This call removes the lock entry corresponding to the given target ID, if
registered by the caller. If a lock is not owned by the caller or does not
exist, the call fails.

If _lock_owner_ is NULL, the owner is not considered. The call fails only if
a lock does not exist.

```c
/**
 * Release a lock.
 *
 * If \a lock_owner is NULL, remove the lock without considering the
 * previous owner.
 *
 * @param[in]   handle          DSS handle.
 * @param[in]   lock_id         Lock identifier.
 * @param[in]   lock_owner      Name of the lock owner.
 * @return                      0 on success,
 *                             -ENOLOCK if the lock does not exist,
 *                             -EACCESS if the lock owner does not match.
 */
int dss_unlock(struct dss_handle *handle, const char *lock_id,
               const char *lock_owner);
```

## Status call
This call retrieves the status of a lock. If the lock does not exist, the call
fails.

If _lock_owner_ is NULL, the string is not allocated.
If _lock_timestamp_ is NULL, the structure is not filled.

```c
/**
 * Retrieve the status of a lock.
 *
 * If \a lock_owner is NULL, the string is not allocated.
 * If \a lock_timestamp is NULL, the structure is not filled.
 *
 * @param[in]   handle          DSS handle.
 * @param[in]   lock_id         Lock identifier.
 * @param[out]  lock_owner      Name of the lock owner, must be freed by
 *                              the caller.
 * @param[out]  lock_timestamp  Date when the lock was taken.
 * @return                      0 on success,
 *                             -ENOMEM if the \a lock_owner string cannot be
 *                              allocated,
 *                             -ENOLOCK if the lock does not exist.
 */
int dss_lock_status(struct dss_handle *handle, const char *lock_id,
                    char **lock_owner, struct timeval *lock_timestamp);
```
