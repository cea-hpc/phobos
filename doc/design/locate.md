% Locate feature

# Overview

This document describes the locate feature, to give users a way to locate on
which node an object is currently reachable, based on its current medium
location.

---

## Use cases
> We consider here a distributed phobos storage system

1. Object location

> User wants to know where an object should be accessed from.

2. Object location on get error

> User wants to know where an object should be accessed from if not available on
> the current phobos node.

3. Medium location

> Admin wants to know from which node a medium is currently reachable to be able
> to access it.

---

## Feature implementation

### API calls
This feature implies some modifications on the Phobos API.

#### Object location
The `phobos_locate()` call retrieve a node name which can be used to access an
object.

```c
/**
 * Retrieve one node name from which an object can be accessed.
 *
 * @param[in]   obj_id      ID of the object to locate.
 * @param[out]  node_name   Name of the node which can give access to \p obj_id.
 * @return                  0 on success,
 *                         -errno on failure.
 * @error       EAGAIN      The object \p obj_id is currently not reachable.
 */
int phobos_locate(const char *obj_id, char **node_name);
```

The call takes an object ID as input and gives back a node name as output. The
retrieved node name is the one targeted by the locate command if it can access
the object or the first one encountered which can reach it. If no nodes can
currently access the object, -EAGAIN is returned.

#### Object retrieval
The `phobos_get()` call remains the same, but it supports a new flag in the
pho_xfer_desc data structure called `PHO_XFER_OBJ_LOCATE`. If this flag is set
and the call failed because the object is not reachable, it will call
`phobos_locate()`.

To retrieve the results of the `phobos_locate()` call, a field is added to the
XFer parameters, which consists in the name of the node to access to make the
`phobos_get()` call.

```c
struct pho_xfer_get_params {
    char *node_name;                    /**< Node name [out] */
};

union pho_xfer_params {
    struct pho_xfer_put_params put;
    struct pho_xfer_get_params get;     /**< GET parameters */
};
```

Whether `PHO_XFER_OBJ_LOCATE` is set or not, the `phobos_get()` call returns
-EREMOTE if the call fails because the object is not reachable.

#### Medium location
The `phobos_admin_medium_locate()` call retrieves the name of the node which
detains the medium, for example where it is mounted (if tape).

```c
/**
 * Retrieve the name of the node which detains a medium.
 *
 * @param[in]   medium_id   ID of the medium to locate.
 * @param[out]  node_name   Name of the node which detains \p medium_id.
 * @return                  0 on success,
 *                         -errno on failure.
 */
int phobos_admin_medium_locate(struct pho_id medium_id, char **node_name);
```

The call takes a medium ID as input and gives back a node name as output. The
retrieved node name is the one targeted by the locate command if the medium is
not detained by another one and reachable by the local node. In the other cases,
it responds with the node which detains the medium or the first one which can
access it.

### CLI calls
'locate' is an action keyword on objects and media, to retrieve the node names
from where we can access them.

```
$ phobos tape locate obj_id
$ phobos dir locate obj_id
```

As for the other object actions, the 'locate' action does not need the 'object'
keyword.

```
$ phobos locate obj_id
```

The command 'phobos get' is given a new option to indicate if the user wants to
know where its object is located if not available.

```
$ phobos get [--locate] obj_id file
```

---

## Responses to use cases
The following section describes CLI calls utilization or internals following the
use cases presented in the first section.

1. Object location

```sh
$ phobos locate obj_foo
Object 'obj_foo' is accessible on node 'st_node_2'
```

2. Object location on get error

```sh
$ phobos get --locate obj_foo foo
ERROR: Object 'obj_foo' can not be retrieved (object is remote)
Object 'obj_foo' is accessible on node 'st_node_2'
$ ssh st_node_2
$ phobos get obj_foo foo
Object 'obj_foo' succesfully retrieved
```

3. Medium location

```sh
$ phobos tape locate P00000L5
Medium 'P00000L5' is accessible on node 'st_node_2'
$ ssh st_node_2
$ phobos tape format P00000L5
Medium 'P00000L5' is now formatted
```

