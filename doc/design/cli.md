# Description

Command Line Interpreter using Python modules and application.

Phobos Application :
    - phobos [options] <object> <verb>

## Libraries

The application mostly relies on the `phobos` module, which itself is
divided into submodules. Some are low level bindings over the C APIs
(using python's C API and `ctypes`), and others are pure python modules
which expose object oriented interfaces for use in the application.

## References
* https://docs.python.org/2/c-api/
* https://docs.python.org/2/library/ctypes.html
