# Layout management module

## C library: libpho_layout

This module abstracts the way a given object is written on medias. The layout
layer emits medium allocation requests to the LRS (Local Resource Scheduler, see
the `doc/design/lrs.txt` documentation) and handles the IOs to these allocated
media.

Various layouts can be implemented, for example: simple (just plain data),
raid1, raid0, compression or various error correction schemes. As some of
these implementations may rely on heavy dependencies, layouts are considered
external plugins and are dynamically loaded into Phobos on first usage.

## Plugin mechanism and registration

A plugin is dynamically loaded by building a dynamic library name from its
plugin name. For example, a module called "simple" will be implemented by a
library called `libpho_layout_simple.so`.

This library has to implement a `pho_layout_mod_register` function that will
fill a `struct layout_module` with the relevent information, including a
vector of functions serving as an entry point to this layout.

## API overview

For more details, see `pho_layout.h`.

At the module level, the two functions `encode` and `decode` allow to
initialize an encoder or decoder that will be responsible for performing a put
or get on one given object.

Encoders and decoders will emit requests to the LRS in order to allocate the
media on which they will perform the necessary IO to put or get an object.
They are implemented around one main method: `step`. Each call to `step`
provides potential answers from the LRS and allows the encoder or decoder to
emit new requests (typically, to release no longer needed media and request
the next media). Currently, all the IO happen inside this `step` function.

The typical (and simplified) `step` pseudo code would be:

```c
if (is_media_allocation(answer)) {
    perform_io(answer.media);
    emit_release_request(answer.media);
}

if (more_io_needed) {
    emit_media_alloc_request(next_media);
} else {
    /* The encoder or decoder is done */
}
```

**Note:** when writing data, the data can only be considered as written when the
media release response message is received by the encoder.

## Dependencies

* `lrs` protocol: layouts directly consume and produce messages defined by the
  LRS protocol (defined in `pho_lrs.h`). See `doc/design/lrs.txt`.
* `io`: abstracts how IOs are performed on various media. See
  `doc/design/adapters.txt`.

## Layout parameters

As specific layouts require specific parameters, one can provide these values
by using the `lyt_params`, either through the CLI or the profiles.

For instance, when using the CLI, a user can give the following command to
put a file using the raid1 layout and a replica count of 1:

```bash
$phobos put --family dir --layout raid1 --lyt-params repl_count=1 file oid
```

Generally, the layout and layout parameters follow this algorithm:

```
If layout is defined
    set layout
    if lyt_params is defined in the same level of configuration or above
        set lyt_params
    else
        use module's default lyt_params
```

For more information regarding the level of configuration, please read
`config.md`.
