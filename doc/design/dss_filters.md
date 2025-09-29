# DSS Filters

## Presentation

The DSS layer exposes a generic API for fetching objects of various nature from
the underlying database.

Retrieval can be refined by filters exposed as JSON objects representing
combinations of the criteria to use.

## Filter format

A filter must be a JSON object or NULL. No alternative. The base unit is the
criteria object, and many can be combined with boolean operators for complex
queries.

See the 'Examples' section below for cool snippets to impress your friends.

## Base criteria

The simplest criteria is equality. It therefore comes with a compact
representation: `{<field_name>: <value>}`.

* field_name: is the public field name, such as 'DSS::OBJ::oid'.
* value: can be a string, integer or real. Type is automatically detected and
  encoded within the JSON object.

Other operators are available. They are used as such:
`{<op>: {<field_name>: <value>}}`.

* $GT: greater than (>)
* $GTE: greater or equal than (>=)
* $LT: less than (<)
* $LTE: less or equal than (<=)
* $LIKE: pattern matching, wildcard is '%'
* $REGEXP: posix regexp matching
* $INJSON: json inclusion test
* $KVINJSON: json key-value inclusion test
* $XJSON: json existence test

## Boolean combinators

The criteria above can be grouped and combined using boolean operators.
The syntax is: {<combo>: [<criteria0>, <criteria1>, ...]}

* $AND: all criteria must be satisfied
* $NOR: all criteria must be NOT satisfied
* $OR: at least one of the criteria must be satisfied

## Examples

* Retrieve unlocked devices:
```
{"DSS::DEV::adm_status": PHO_RSC_ADM_ST_UNLOCKED}
```

* Retrieve unlocked tape drives:
```
{"$AND": [
    {"DSS::DEV::adm_status": PHO_RSC_ADM_ST_UNLOCKED},
    {"DSS::DEV::family": PHO_DEV_TAPE}
]}
```

* Retrieve unlocked tapes with free space >=10GB, formatted and not full:
```
{"$AND": [
  {"DSS::MDA::family": PHO_DEV_TAPE},
  {"DSS::MDA::adm_status": PHO_RSC_ADM_ST_UNLOCKED},
  {"$GTE": {"DSS::MDA::vol_free": 10737418240}},
  {"DSS::MDA::lock": ""},
  {"$NOR": [
    {"DSS::MDA::fs_status": PHO_FS_STATUS_BLANK},
    {"DSS::MDA::fs_status": PHO_FS_STATUS_FULL}
  ]}
]}
```
