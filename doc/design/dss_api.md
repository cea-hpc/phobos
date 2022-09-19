% DSS API

# General purpose
This document will address the design discussions around the phobos Distributed
State Service (DSS).

As discussed for now, our goal is to design the DSS as an API which provides
simple commands such as GET, SET and LOCK.

# Generic LOCK
The generic lock calls allow the user to notify they want to do some critical
manipulations that require an exclusive access to data, resources, etc.

## Lock fields

The lock identifier/type must be a unique pair. While the type is restricted
to a specific list, the identifier can be any string, and it is up to the
caller to know what this string refers to.

The type of a lock represents the resource to be locked as viewed by the DSS.
These resources are currently: media (DSS_MEDIA), devices (DSS_DEVICE),
objects (DSS_OBJECT), layouts (DSS_LAYOUT), deprecated objects (DSS_DEPREC).

The owner is an integer identifying who owns the lock on a particular hostname.
It corresponds to the pid of the process asking for a lock.

A lock is taken on a specific identifier/type pair, and belongs to a specific
hostname/owner pair. The latter will be determined when the lock is set,
and will match the caller's hostname/pid.

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
 *
 * @return                      0 on success,
 *                             -EEXIST if one of the targeted locks already
 *                              exists.
 */
int dss_lock(struct dss_handle *handle, enum dss_type type,
             const void *item_list, int item_cnt);
/**
 * Take locks on a specific hostname.
 *
 * If any lock cannot be taken, then the ones that already are will be
 * forcefully unlocked, and the function will not try to lock any other
 * ressource (all-or-nothing policy).
 *
 * @param[in]   handle          DSS handle.
 * @param[in]   type            Type of the ressources to lock.
 * @param[in]   item_list       List of ressources to lock.
 * @param[in]   item_cnt        Number of ressources to lock.
 * @param[in]	hostname        Hostname of the lock to set
 *
 * @return                      0 on success,
 *                             -EEXIST if one of the targeted locks already
 *                              exists.
 */
int dss_lock_hostname(struct dss_handle *handle, enum dss_type type,
             	      const void *item_list, int item_cnt,
                      const char *hostname);
```

## Refresh call
This call updates the timestamps of the entries referenced by the given locks.

If a specified lock does not exist or is not possessed by the caller,
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
 *
 * @return                      0 on success,
 *                             -ENOLCK if the lock does not exist,
 *                             -EACCES if the lock owner does not match.
 */
int dss_lock_refresh(struct dss_handle *handle, enum dss_type type,
                     const void *item_list, int item_cnt);
```

## Unlock call
This call removes the lock entries corresponding to the given target IDs, if
registered by the caller. If a lock is not owned by the caller or does not
exist, the lock is not removed.

```c
/**
 * Release locks.
 *
 * If \p force_unlock is true, the lock's hostname and owner are not matched
 * against the caller's.
 *
 * The function will attempt to unlock as many locks as possible. Should any
 * unlock fail, the first error code obtained will be returned after
 * attempting to unlock all other locks (as-much-as-possible policy).
 *
 * @param[in]   handle          DSS handle.
 * @param[in]   type            Type of the ressources to unlock.
 * @param[in]   item_list       List of ressources to unlock.
 * @param[in]   item_cnt        Number of ressources to unlock.
 * @param[in]   force_unlock    Whether we ignore the lock's hostname and owner
 *                              or not.
 *
 * @return                      0 on success,
 *                             -ENOLCK if the lock does not exist,
 *                             -EACCES if the lock owner does not match.
 */
int dss_unlock(struct dss_handle *handle, enum dss_type type,
               const void *item_list, int item_cnt, bool force_unlock);
```

## Status call
This call retrieves the status of locks.

If _locks_ is NULL, the structures are not filled.

```c
/**
 * Retrieve the status of locks.
 *
 * If \p locks is NULL, the structures are not filled.
 *
 * The function will attempt to query the status of as many locks as possible.
 * Should any query fail, the first error code obtained will be returned after
 * attempting to query all other locks's statuses (as-much-as-possible policy).
 *
 * @param[in]   handle          DSS handle.
 * @param[in]   type            Type of the ressources's lock to query.
 * @param[in]   item_list       List of ressources's lock to query.
 * @param[in]   item_cnt        Number of ressources's lock to query.
 * @param[out]  locks           List of \p item_cnt structures, filled with
 *                              each lock owner, hostname and timestamp, must
 *                              be cleaned by calling pho_lock_clean.
 *
 * @return                      0 on success,
 *                             -ENOMEM if a \p lock_owner string cannot be
 *                              allocated,
 *                             -ENOLCK if a lock does not exist.
 */
int dss_lock_status(struct dss_handle *handle, enum dss_type type,
                    const void *item_list, int item_cnt,
                    struct pho_lock *locks);
```
