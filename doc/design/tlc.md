# Tape Library Controller

## Goals

Some tape libraries do not support sending multiple SCSI commands in parallel.
Since Phobos has a distributed architecture, multiple nodes may send requests at
the same time. Furthermore, since Phobosd is multi threaded, a single node can
send multiple requests as well.

To solve this issue, every SCSI request issued by any Phobos component must be
handled by one entity and serialized when necessary. Some libraries may support
concurrent requests. For those libraries, this entity doesn't need to serialize
the requests. This entity will be named *Tape Library Controller (TLC)*.

Since the TLC will receive every SCSI command, it is the only component that
will need to know the state of the library. It can therefore scan the library
only when necessary and store the data in its memory. For instance,
`phobos lib scan` will query the state from the TLC which will simply send its
in-memory copy to the requesting client.

Therefore, the TLC will handle two tasks:
- collect and execute SCSI requests from any Phobos component while taking
  library restrictions into account;
- keep the state of the library in memory instead of querying the state on each
  move as is the case currently.

## Changes required

1. communication over a TCP socket
2. define a protocol between the TLC and its clients
3. scan the library and update the cache
4. handle load and unload requests

### General considerations

Like Phobosd, this component will be a system daemon running on a server that
has access to the library.

#### High Availability

Since the TLC will be an essential and central component of Phobos when using
tapes, some HA mechanisms need to be in place to make Phobos more resilient.
Phobos does not have HA mechanisms built into it, so it is the responsibility of
the system administrator to make sure that this component stays available. Note
that without the TLC, Phobosd cannot move tapes or query the state of the
library, commands such as `phobos lib scan` cannot work, etc.

### TCP communication

Since this is the first component that needs to communicate with every node of
the system, we need to extend the *communication* module to support TCP sockets.

A new section will be added to the configuration **[tlc]**:

```
[tlc]
# will be used by the server and client
hostname = tlc
port = 1234

# lib_device is only useful to the TLC
lib_device = /dev/changer
```

The function `pho_comm_open` will have to be extended to support TCP sockets.

```
union pho_comm_addr {
    struct {
        const char *path;
    } unix;
    struct {
        const char *hostname;
        int port;
    } tcp;
};

enum pho_comm_socket_type {
    PHO_COMM_UNIX_SERVER,
    PHO_COMM_UNIX_CLIENT,
    PHO_COMM_TCP_SERVER,
    PHO_COMM_TCP_CLIENT,
};

/**
 * Open a socket
 *
 * \param[out]      ci          Communication info to be initialized.
 * \param[in]       addr        Address of the server.
 * \param[in]       type        Which type of socket we are opening.
 *
 * \return                      0 on success, negative POSIX error on failure
 */
int pho_comm_open(struct pho_comm_info *ci, const union pho_comm_addr *addr,
                  enum pho_comm_socket_type type);
```

Note: the structure `pho_comm_info` contains the path to the UNIX socket but
doesn't seem to be used outside of `pho_comm_open` and `pho_comm_close`. This
member will have to be removed to support both types of sockets.

Note2: Some parts of the communication module may need to be updated to handle
network failures.

### Client requests

The TLC should be able to:

- load a tape;
- unload a tape;
- query the status of a drive;
- query the state of the library;
- reload the cache on demand.

To prevent programming errors, the TLC should check the status of a tape or
drive before loading or unloading a tape. An administrator should not be able to
unload a tape that is currently in use (i.e. locked in the DSS). Phobosd should
only be able to ask movements on drives that it owns for tapes that it owns
(administratively unlocked and with a concurrency lock with the same host).

In the following sections, the name of the drive or tape refers to the name
stored in the DSS that is part of the primary key. Since the TLC will only
manage tapes and drives, the name is enough to uniquely identify an element.
Although this assumption may change in the future when supporting multiple
libraries.

**Load/Unload:** load a tape to a given drive.

- arguments:
    - name of the drive
    - name of the tape (this is not necessary for unloads but can be
                        useful to validate the request)
- response:
    - status of the operation: 0 on success, an error code otherwise

**Drive lookup:** lookup the current state of the drive.

- arguments:
    - name of the drive
- response:
    - status of the operation: 0 on success, an error code otherwise
    - result of the query (cf. `struct lib_drv_info`)

**Lib scan:** (cf. function `ldm_lib_scan`) scan the content of the library.

- arguments:
    - boolean indicating whether the TLC should reload its state or not
- response:
    - status of the operation: 0 on success, an error code otherwise
    - the state of the library: currently, we return it as a `json_t` element
      which means that it will have to be transformed into a string. We can also
      use Protobuf to serialize the `struct element_status`. The second option
      is more complex but more efficient for the data transfer.

**Reload cache:** ask the TLC to reload its internal cache. This will be useful
to notify the TLC when tapes are added to the library.

- arguments:
    - none
- response:
    - status of the operation: 0 on success, an error code otherwise

**Ping:** if up, the TLC will query the library to see if it is up as well.
The TLC will perform an operation similar to what the `loaderinfo` command does.

- empty message
- response:
  - boolean: `library_is_up`: true if the TLC could successfully contact the
    library. False, otherwise.

If the client does not receive a response after a given time, it can assume that
the TLC is not available. If a response is received, the boolean will indicate
whether the library is accessible or not.

**Error Response:**

- sent by the TLC only as a response to an invalid or failed request

Note: these messages do not include the name of the library to query. This
design assumes that once Phobos supports multiple libraries, one TLC instance
will be running for each library. Therefore, only the client has to worry about
which library it wants to use and therefore which TLC it should contact. This is
also the reason why specifying the tape on unload can be useful to prevent
errors.

### New features

Since Phobos wants to gather every request to the library, the system
administrator should avoid using external tools to manipulate tapes. Therefore,
Phobos should provide additional commands to load and unload a tape.

```
$ phobos drive load /dev/sg1 P0000L5
```

```
# The name of the tape is not necessary here and can be made optional. It can be
# used as a safe check.
$ phobos drive unload /dev/sg1 P0000L5

# To empty every drive
$ phobos drive unload --all
```

Query the state of a drive (cf. `ldm_lib_drive_lookup`):
```
$ phobos drive lookup /dev/sg1 # Or serial number
P0000L5
```

Note: the lookup command is not necessary but will not be that much harder to
implement once tape load/unload are.

Note2: when supporting multiple libraries, these commands will need to have a
parameter to specify which library is concerned.

`phobos lib scan` will query the TLC's internal state. An option can be added to
ask the TLC to reload its internal state:

```
# Reload the state before sending it to the client
$ phobos lib scan --reload

# Simply reload the state of the library
$ phobos lib reload
```

Note: `phobos lib reload` may not be necessary. A request to reload the cache
can be sent to the TLC after a `phobos tape add` for example but this can lead
to more queries than necessary if several `phobos tape add` are executed. This
may not be an issue if the TLC is able to deduplicate these requests.

Add support to `phobos ping` for TLC:

```
$ phobos ping phobosd
$ phobos ping tlc
```

## Internals of the TLC

The TLC should be able to receive request from Phobos clients and send SCSI
requests to the library independently. Therefore, it should have a thread
dedicated to communication. The pseudo code below indicates how it should
behave:

```
void tlc_communication()
{
    while (true) {
        if (should_stop())
            break;

        recv(&requests);
        while (!requests.empty()) {
            # thread safe queue
            ts_work_queue.push(requests.pop());
        }

        while (!ts_responses.empty()) {
            # thread safe queue
            send(ts_responses.pop());
        }
    }
}
```

Note: like Phobosd, the TLC can be stopped at any time by a SIGTERM or SIGINT.
To make the code simpler, we could make the assumption that the signal is only
checked at the beginning of this loop (`should_stop`).

The `ts_work_queue` is a queue that will be shared with the thread that will
perform the requests. This queue is protected by a mutex as is already the case
for some queues in Phobosd. This queue will be inspected by another thread that
will read the requests, check their correctness and perform actions if
appropriate.

Below is a pseudo code describing the behavior of the thread consuming work:

```
void tlc_dispatcher()
{
    library_cache = load_cache();

    while (true) {
        if (should_stop())
            break;

        if (ts_work_queue.empty())
            continue;

        request = ts_work_queue.pop();
        if (request.is_invalid()) {
            ts_responses.push(invalid_request_error(request));
            continue;
        }

        dispatch(request);
    }

    clean_cache(library_cache);
}
```

The key to the asynchronicity (or not) of the handling of requests lies in
the dispatch function. This function can do the actual operation, in which case
every request will be serialized. Or this function can perform some requests
asynchronously by spawning threads under the hood. These threads are isolated
and will not exchange information with other threads. The dispatch function will
be responsible for putting the response back into the `ts_responses` queue once
the operation is finished. This is done so that the dispatcher thread knows
whether an operation has succeeded or not and when. It will therefore be able to
update its internal cache in the same thread so that we avoid extra
synchronization mechanisms.

We can also imagine having a dispatch function that will buffer some requests
and reorder them if appropriate. If a library can move several tapes
concurrently, the dispatch function can perform them concurrently for example.
This dispatch function may be specific to the given library to manipulate it
appropriately.

The `ts_work_queue` and `ts_responses` will contain the following structure:

```
struct tlc_work {
    pho_req_t *request;
    pho_resp_t response;
    union {
    } params; /** This union will contain information specific to certain types
                * of requests.
                */
};
```

- `tlc_work::request` is a pointer to the request received using
  `pho_comm_recv`;
- `tlc_work::response` is the response that will be sent. It will either be
  the response corresponding to the request or an error;
- `tlc_work::params` will contain additional parameters that may need to be
  stored alongside the request. This union will contain elements specific to
  different types of requests. Whether this component is useful or not will
  depend on implementation details;
- additional elements may be required such as the socket ID of the client as in
  `req_container` for Phobosd. The goal of this design is not to be exaustive.

This structure will be allocated for each new request received by the
communication thread and freed when the response is sent back. Therefore, other
threads will not have to free them. That memory will be managed by the
communication thread.

### Some asynchronous considerations

From previous testing, it seems that a library can handle several moves
concurrently but any concurrent lookup is blocked while the moves are performed.
This resulted in a lot of timeouts since the timeout of a lookup is much smaller
than for moves.

The TLC can expose some parameters to specify how it should handle requests. The
following is a sample of some possible parameters that may or may not be
relevant in practice:

```
[tlc]
# How the TLC should execute SCSI requests
# "synchronous" or "asynchronous"
request_handling_policy = synchronous

# The following parameters are only relevant if the policy is "asynchronous"

# Maximum number of SCSI requests (> 0)
max_scsi_requests = 10

# Maximum number of concurrent moves (> 0)
max_moves = 4

# Maximum number of concurrent lookups (> 0)
max_lookups = 2

# Whether the TLC can do a lookup while one or several moves are executed
# "yes" or "no"
# Defaults to "no" since this is the safest behavior
concurrent_moves_and_lookups = yes
```

## Cache management

A first implementation may serialize every requests. In this case, the
`tlc_dispatcher` thread will be the only thread doing the actual work.

If the dispatch function wants to execute tasks asynchronously, it will be
responsible for waiting for results. In any case, the dispatch function will
push the response in the `ts_responses` queue when available. Therefore, only
the dispatch thread will exchange with the communication one. Since the result
of an asynchronous operation is returned to the dispatch thread, the cache can
be updated locally without needing extra synchronization. It also means that the
behavior of the overall system does not depend on how the requests are executed.

Once a successful move has finished, the dispatch function will update the
cache.

Since the state of the library is only loaded at startup or when explicitly
asked by a request, we may receive a request to load a tape that is not in the
cache. In this case, the TLC can choose to reload the whole cache and then see
if the tape has been added.

Extra functions can be executed when an unknown tape is requested to notify the
administrator that a tape is missing from the system. A path to a script to
execute in such case can be specified in the configuration that will take the
name of the tape as a first argument. The library name will also be useful when
supporting multiple libraries.

Extra information may be attached to each elements of the library in the cache
so that dispatch functions can keep track of busy slots (the ones where a move
is executed asynchronously) to avoid launching concurrent moves on the same
drive for example. This extra information should be managed by the dispatch
function in the `tlc_dispatcher` thread to avoid extra synchronization.

## LDM module changes

Since the TLC will manage the internal state of the library, any component that
wants to load or unload a tape won't have to care about its address. They will
simply request to load a tape into a drive or unload the tape that is currently
inside a drive.

`pho_lib_adapter_module_ops::lib_scan` will have to be adapted to expose the
`struct status_array` that contains the state of the library.

`pho_lib_adapater_module_ops::lib_media_lookup` and
`pho_lib_adapater_module_ops::lib_media_move` both use a structure of type
`lib_item_addr`. This structure is only useful for the tape family and will have
to be removed from the `pho_lib_adapter_module_ops` API since the client of the
TLC will not need to know where a tape is to load or unload it. The
`lib_media_lookup` function may not be necessary anymore on the client side.

But the TLC will need to use these functions to manage the library. Since the
TLC is specific to tapes, it won't need to use the module API and will be able
to link to the functions of the LDM lib SCSI directly. Therefore, these
functions as defined by the LDM lib SCSI module will still be of use but the LDM
module API will be updated.

## Developments

1. create the Protobuf messages;
2. add those messages to the serialization module;
3. update the communication module to support TCP sockets;
4. create the basic structure of the TLC that is able to answer Ping requests;
5. make the TLC load the cache and implement the lib scan request;
6. implement load, unload, lookup;
7. create a client LDM module to communicate with the TLC;
8. update the LDM module API to remove the address and expose the
  `struct status_array`;
9. implement CLI interfaces described above and the corresponding C admin API
   functions;
10. update the LRS module to use the new LDM API.
