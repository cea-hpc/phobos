# Alias

An alias in Phobos is a combination of put parameters, grouped together under a
user-defined name for convenience.
These parameters are:
* layout
* resource family
* tags

The goal is to help users create a more convenient workflow by specifying their
own names for recurring tasks.

## Definition

The alias can be defined in the Phobos config file in the form:

```
[alias "alias_name"]
family = dir
layout = simple
tags = tag1,tag2
```

It is possible to create multiple aliases.
The order of the parameters above does not matter and not all of them have to be
specified.

Once defined the alias can be used by either providing the name as a parameter
to the cli like this

```
phobos put -a alias_name file objectid
```
or
```
phobos put --alias alias_name file objectid
```

or by setting the `alias` value in `pho_xfer_put_params` to the name of the
alias.

If an alias is provided together with other parameters like the family or the
layout, these specifically set values will be used instead of the alias.
If tags are provided additionally, these will be joined with the tags from the
alias.

## Example

The following alias is part of the `phobos.conf`

```
[alias "long_term"]
family = tape
tags = long_term_1,long_term_2
```

The command `phobos put -a long_term my_file object-my_file` will create the
object `object-my_file` on a tape with the tags `long_term_1,long_term_2` using
the default layout.

The command `phobos put -a long_term -l simple my_file object-my_file` will
create the object `object-my_file` on a tape with the tags
`long_term_1,long_term_2` using the **simple** layout.

The command `phobos put -a long_term -f dir my_file object-my_file` will create
the object `object-my_file` using the default layout on a **directory** with the
tags `long_term_1,long_term_2`, therefore overwriting the family of the alias.

The command `phobos put -a long_term -T new_tag my_file object-my_file` will
create the object `object-my_file` with the default layout on a tape with the
tags `long_term_1,long_term_2,new_tag`, therefore extending the tags of the
alias with the new one.
