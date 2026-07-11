# zlib-static

Windows/MSVC builds expect static zlib and minizip files in this folder.
OpenHi2txt defines `ZLIB_STATIC` when using these files so zlib symbols are not
declared as DLL imports.

Expected layout:

```text
thirdparty/zlib-static/
  include/
    zlib.h
    zconf.h
    minizip/
      unzip.h
      ioapi.h
      ...
  lib/
    zs.lib
    minizip.lib
```

Build both libraries with the same MSVC runtime setting used by OpenHi2txt and
most consuming applications: static libraries using the dynamic MSVC runtime
(`/MD`), not `/MT`.
