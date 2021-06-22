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
This call registers new entries for the given locks. If any of the specified
locks are already registered, the call fails and unlocks all already locked
resources.

This call needs to be atomic to prevent two callers from registering the same
lock at the same time. If it happens, the 'first' call will succeed, the
'second' one will fail.

```c
/**
 * Take locks.
 *
 * If any lock cannot be taken, then the ones that already are will be
 * forcefully unlocked, and the function will not try to lock any other
 * ressource (all-or-nothing policy).
 *
 * @param[in]   handle          DSS handle.
 * @param[in]   type            Type of the ressources to lock.
 * @param[in]   item_list       List of ressources to lock.
 * @param[in]   item_cnt        Number of ressources to lock.
 * @param[in]   lock_owner      Name of the lock owner.
 * @return                      0 on success,
 *                             -EEXIST if \a lock_id already exists.
 */
int dss_lock(struct dss_handle *handle, enum dss_type type,
             const void *item_list, int item_cnt, const char *lock_owner);
```

## Refresh call
This call updates the timestamps of the entries referenced by the given locks.

If a specified lock does not exist or is not possessed by the given owner,
the lock is not refreshed.

```c
/**
 * Refresh lock timestamps.
 *
 * The function will attempt to refresh as many locks as possible. Should any
 * refresh fail, the first error code obtained will be returned after
 * attempting to refresh all other locks (as-much-as-possible policy).
 *
 * @param[in]   handle          DSS handle.
 * @param[in]   type            Type of the ressources's lock to refresh.
 * @param[in]   item_list       List of ressources's lock to refresh.
 * @param[in]   item_cnt        Number of ressources's lock to refresh.
 * @param[in]   lock_owner      Name of the lock owner, must be specified.
 * @return                      0 on success,
 *                             -ENOLCK if the lock does not exist,
 *                             -EACCES if the lock owner does not match.
 */
int dss_lock_refresh(struct dss_handle *handle, enum dss_type type,
                     const void *item_list, int item_cnt,
                     const char *lock_owner);
```

## Unlock call
This call removes the lock entries corresponding to the given target IDs, if
registered by the caller. If a lock is not owned by the caller or does not
exist, the lock is not removed.

If _lock_owner_ is NULL, the owner is not considered.

```c
/**
 * Release locks.
 *
 * If \a lock_owner is NULL, remove the locks without considering the
 * previous owner.
 *
 * The function will attempt to unlock as many locks as possible. Should any
 * unlock fail, the first error code obtained will be returned after
 * attempting to unlock all other locks (as-much-as-possible policy).
 *
 * @param[in]   handle          DSS handle.
 * @param[in]   type            Type of the ressources to unlock.
 * @param[in]   item_list       List of ressources to unlock.
 * @param[in]   item_cnt        Number of ressources to unlock.
 * @param[in]   lock_owner      Name of the lock owner, ignored if NULL.
 * @return                      0 on success,
 *                             -ENOLCK if the lock does not exist,
 *                             -EACCES if the lock owner does not match.
 */
int dss_unlock(struct dss_handle *handle, enum dss_type type,
               const void *item_list, int item_cnt, const char *lock_owner);
```

## Status call
This call retrieves the status of locks.

If _lock_owner_ is NULL, the strings are not allocated.
If _lock_timestamp_ is NULL, the structures are not filled.

```c
/**
 * Retrieve the status of locks.
 *
 * If \a lock_owner is NULL, the strings are not allocated.
 * If \a lock_timestamp is NULL, the structures are not filled.
 *
 * The function will attempt to query the status of as many locks as possible.
 * Should any query fail, the first error code obtained will be returned after
 * attempting to query all other locks's statuses (as-much-as-possible policy).
 *
 * @param[in]   handle          DSS handle.
 * @param[in]   type            Type of the ressources's lock to query.
 * @param[in]   item_list       List of ressources's lock to query.
 * @param[in]   item_cnt        Number of ressources's lock to query.
 * @param[out]  lock_owner      Name of each lock owner, must be freed by
 *                              the caller.
 * @param[out]  lock_timestamp  Date when each lock was taken or last refreshed.
 * @return                      0 on success,
 *                             -ENOMEM if a \a lock_owner string cannot be
 *                              allocated,
 *                             -ENOLCK if a lock does not exist.
 */
int dss_lock_status(struct dss_handle *handle, enum dss_type type,
                    const void *item_list, int item_cnt,
                    char **lock_owner, struct timeval *lock_timestamp);
```
