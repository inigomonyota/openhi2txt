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

## libchdr

- Source: https://github.com/inigomonyota/libchdr
- Upstream: https://github.com/rtissera/libchdr
- Copyright: Romain Tisserand and contributors
- License: BSD 3-Clause License
- License text: `thirdparty/libchdr/LICENSE.txt`

OpenHi2txt pins a fork of libchdr and builds it statically with its optional
CD/GD-ROM support disabled. General-purpose CHD codecs remain enabled.

The pinned libchdr source includes these additional decoder components:

- LZMA SDK 25.01, placed in the public domain; notice at
  `thirdparty/libchdr/deps/lzma-25.01/LICENSE`
- Zstandard 1.5.7 decompressor, available under its BSD-style license or GPLv2;
  licensing notice retained in `thirdparty/libchdr/deps/zstd-1.5.7/zstddeclib.c`
- dr_flac, available under the public domain dedication or MIT-0;
  license text retained in `thirdparty/libchdr/include/dr_libs/dr_flac.h`
