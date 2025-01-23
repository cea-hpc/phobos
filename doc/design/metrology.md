# Metrology

Retrieving internal Phobosd statistics is useful for monitoring server load,
request scheduling, drive status, and overall system performance. These metrics
provide valuable insights for evaluating the system load, optimizing resource
usage, and detecting anomalies. Additionally, they can be used to populate
dashboards, offering real-time visualization of performance indicators.

## Requirements

The system must be able to query internal information from Phobosd, such as
received requests, device status, database, and scheduling details.

The feature must enable easy integration with common statistics collectors, and
follow the best pratices for defining, naming, and formatting metrics.

## Overall architecture

This initial design focuses on retrieving metrics from each Phobosd instance. It
does not include metric aggregation for a complete instance or the collection of
statistics for features running on the client side of Phobos (e.g., Phobos
Client API, including layouts and I/O adapters, S3 server, lhsmd_phobos, etc.).
Client-side statistics collection should be handled independently by the
clients; however, the API must be instrumented to support this. This will be
part of a complementary design. Collecting statistics from the TLC can be done
the same way as Phobosd.

Two options could be considered for collecting Phobosd statistics:
- Integrating a Prometheus exporter directly into Phobosd. This would enable
  immediate integration of Phobos into a monitoring system. However, this
  approach is not ideal as it tightly couples Phobos monitoring to the Prometheus
  tool, potentially limiting the use of other collection methods. Moreover,
  embedding an HTTP server in Phobosd raises security concerns and adds complexity
  to securing access to this server.
- Providing a CLI command to retrieve statistics. This approach is more
  flexible, as it allows retrieving statistics in multiple formats
  (human-readable, parsable by a shell script, or ingestible by a log collection
  tool). Thus, we choose this latter choice.


The Phobos admin client will query statistics from Phobosd by extending the
existing "monitor" call of the protocol. This RPC currently takes a device
family as argument (e.g. 'dir', 'tape'...), and returns the status of the
corresponding drives in JSON format. While the JSON output format is suitable
for returning any daemon statistics, the input parameters may need to be
modified to meet the requirements for this design.

### Statistics types
In accordance with the types of metrics expected by statistics collection tools,
Phobos will maintain two categories of metrics:
- Counters: a metric that represents a cumulative value that only increases over
  time (e.g, number of requests, number of extents written, total volume of data
  sync'ed...)
- Gauge: a metric that represents a value that can go up or down over time (e.g,
  number of pending requests).

Counters and gauges are 64 bits unsigned integers, as we consider all needed
values can be stored as such. E.g. sizes can be stored up to 16EB, and
nanosecond durations up to 584k years.

### Statistics organization
The statistics are organized into different namespaces, according to the
software layer they provide information for. The choice is made to name these
namespaces not according to the software layer they represent, but according to
their function, in order to make them more easily understandable for the user.
For example, statistics from the lrs.c file, which relate to requests received
by Phobosd, will be grouped under a "req" namespace. Other envisioned
namespaces are: "sched" for statistics related to IO scheduling, and "dev" for
statistics related to devices, "db" for the DSS.

Statistics tools expect that the filters used to query the data and construct
the graphs are identified by tags (in the format tag=value). For example, one
might want to distinguish the requests received by Phobos based on their family,
request type, parameters (sync/no_sync, etc.). These different elements should
be specified as tags. Thus, instead of having metrics like req.nb_read, we
would rather have something like:
```
action=read,family=tape,param=no_sync req.nb=1234
action=read,family=tape,param=no_sync req.time_sec=236782
action=write,family=tape,param=sync   req.nb=2345
action=write,family=tape,param=sync   req.time_sec=536132
```

## Detailed design

### Command line

The command to retrieve statistics from the local phobosd uses the following
syntax:
```
phobos stats [namespace[.name]] [--filter <tag>=<value>[,<tag>=<value>...]] \
             [--format={lines|json}] [--tlc[=<tlcname>]]
```

Notes:
  - For better convenience, all matchings (namespace, name, tags...) are case
  insensitive.
  - The 'tlc' option requests stats from a TLC rather than the main Phobos
  daemon. If no TLC name is specified, this retrieves stats from the default
  TLC.

Examples:
```
# retrieve all stats from the local phobosd, as json
phobos stats --format=json
# retrieve all stats about read requests for tapes
phobos stats req --filter action=read,family=tape
# retrieve all requests counts for tapes
phobos stats req.nb --filter family=tape
# retrieve SCSI call stats from the TLC
phobos stats scsi --format=json --tlc
```

### Internal API
Collecting metrics internally within the Phobos daemon (phobosd) requires
handling the two categories of metrics previously mentioned: 'counter' and
'gauge'. The values of these metrics are integers.
To ensure consistency in a multithread environment, it is also
necessary to manage these metrics using atomic instructions.

The following calls are used internally to create and update metrics:
```
# create and register a new metric
pho_stat_t* pho_stats_create(type, namespace, name, tags);

# increment a metric of type 'integer' (can be used for both counter and gauge)
void pho_stats_incr(pho_stat, int_value);

# set the value for an integer gauge
void pho_stats_set(pho_stat, int_value);

The following calls are used for retrieving stats:
```
# initialize a stats iterator with optional namespace, name and tags
pho_stat_iter_t *pho_stats_iter_init(namespace, name, tags);

# get the next metric from the iterator
pho_stat_t* pho_stats_iter_next(iter);

# close the iterator
pho_stats_iter_close(iter);
```
