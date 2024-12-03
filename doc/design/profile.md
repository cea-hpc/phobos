# Profile

A profile in Phobos is a combination of put parameters, grouped together under a
user-defined name for convenience.
These parameters are:
* layout
* resource family
* tags

The goal is to help users create a more convenient workflow by specifying their
own names for recurring tasks.

## Definition

The profile can be defined in the Phobos config file in the form:

```
[profile "profile_name"]
family = dir
layout = simple
tags = tag1,tag2
```

It is possible to create multiple profiles.
The order of the parameters above does not matter and not all of them have to be
specified.

Once defined the profile can be used by either providing the name as a parameter
to the cli like this

```
phobos put --profile profile_name file objectid
```

or by setting the `profile` value in `pho_xfer_put_params` to the name of the
profile.

If a profile is provided together with other parameters like the family or the
layout, these specifically set values will be used instead of the profile.
If tags are provided additionally, these will be joined with the tags from the
profile.

## Default profiles

Users can define a default profile that will be used to specify any parameter
in case none was given/found in the same or higher levels of configuration.

For more information regarding the levels of configuration, please read
`config.md`.

## Example

The following profile is part of the `phobos.conf`:

```
[profile "long_term"]
family = tape
tags = long_term_1,long_term_2
```

The command `phobos put --profile long_term my_file object-my_file` will create
the object `object-my_file` on a tape with the tags `long_term_1,long_term_2`
using the default layout.

The command `phobos put --profile long_term --layout simple my_file
object-my_file` will create the object `object-my_file` on a tape with the tags
`long_term_1,long_term_2` using the **simple** layout.

The command `phobos put --profile long_term --family dir my_file object-my_file`
will create the object `object-my_file` using the default layout on a
**directory** with the tags `long_term_1,long_term_2`, therefore overwriting
the family of the profile.

The command `phobos put --profile long_term --tags new_tag my_file
object-my_file` will create the object `object-my_file` with the default layout
on a tape with the tags `long_term_1,long_term_2,new_tag`, therefore extending
the tags of the profile with the new one.
