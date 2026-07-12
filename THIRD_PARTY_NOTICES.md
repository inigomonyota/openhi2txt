# Third-Party Notices

OpenHi2txt includes or links the following third-party software.

## RapidXML 1.13

- Source: https://rapidxml.sourceforge.net/
- Copyright: Marcin Kalicinski
- License: Boost Software License 1.0 or MIT License, at the user's choice
- License text: `thirdparty/rapidxml-1.13/LICENSE`

RapidXML is included as a header-only XML parser.

## zlib

- Source: https://zlib.net/
- Copyright: Jean-loup Gailly and Mark Adler
- License: zlib License
- License text: `thirdparty/zlib-static/LICENSE`

Windows/MSVC builds link a bundled static x64 `/MD` library. Other supported
platforms use the zlib supplied by the system or selected by CMake.

## minizip

- Source: the classic minizip implementation distributed with zlib
- Copyright: Gilles Vollant and contributors
- License: zlib License
- License notices: retained in `thirdparty/zlib-static/include/minizip/unzip.h`
  and `thirdparty/zlib-static/include/minizip/ioapi.h`

Windows/MSVC builds link a bundled static x64 `/MD` library. Other supported
platforms use the minizip supplied by the system or selected by CMake.
