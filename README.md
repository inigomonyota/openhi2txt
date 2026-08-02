# OpenHi2txt

OpenHi2txt is an open-source C++ high-score reader compatible with hi2txt XML
definitions. It is both:

- an embeddable C++ library for frontends, launchers, tools, and scrapers
- an optional command-line executable for hi2txt-compatible CLI workflows

Build: CMake with Visual Studio 2022 (v143) x64, Ninja, or another C++17 compiler.

Detailed library and CLI usage is documented in [docs/usage.md](docs/usage.md).
Definition authors should use the
[OpenHi2txt definition XML reference](docs/xml-reference.md).

## Definition Compatibility

Existing definitions keep the original hi2txt root and behavior unchanged:

```xml
<!DOCTYPE hi2txt SYSTEM "hi2txt.dtd">
<hi2txt label="Example">
    <!-- definition -->
</hi2txt>
```

Definitions that use openhi2txt-only functionality use an alternative root and
declare the minimum openhi2txt version they require:

```xml
<!DOCTYPE openhi2txt>
<openhi2txt requires="0.2.0" label="Example" ingame-score="true">
    <!-- definition -->
</openhi2txt>
```

`openhi2txt` accepts all attributes and children currently accepted by the
legacy `hi2txt` root, plus the mandatory `requires="MAJOR.MINOR.PATCH"`
attribute. It rejects a definition before processing when the required version
is newer than the running version. Use the legacy root until a definition
actually depends on an openhi2txt extension.

Version 0.2.0 adds two opt-in definition extensions: terminated loops with
`loop@stop-when` and positional output lookup with
`column@source-row="output_index"`. Neither changes behavior when omitted.

## Use It As A Library


```cpp
#include <openhi2txt/openhi2txt.h>
```

Library target:

```cmake
target_link_libraries(your_app PRIVATE openhi2txt::openhi2txt)
```

The public API returns ordinary C++ containers and structs. Typical frontend
startup code can bulk-load persisted score XML with `readAllPersistedGames()`,
then call `refreshGame()` after a game exits to decode live `.hi` or nvram data.

## Use It As A CLI Tool

The CLI is built by default and produces an `openhi2txt` executable:

```text
openhi2txt -d hi2txt.zip -m <mame> -g <romname> -xml
```

Original hi2txt-style arguments are also accepted, including `-descr`/`-ds`,
`-hiscoredat`/`-hs`, display filters, listing flags, and positional
`<hi_file_path>` input.

Trace/debug command:

```text
openhi2txt -d hi2txt.zip -m <mame> -g <romname> -xml -trace
```

The executable is optional. Disable it when embedding only the library:

```bash
cmake -S . -B build -DOPENHI2TXT_BUILD_CLI=OFF
```

## Building

Windows with Visual Studio 2022:

```powershell
cmake --preset windows-msvc
cmake --build --preset windows-msvc
```

The Windows/MSVC x64 build uses `/MD` static zlib/minizip files under
`thirdparty/zlib-static/lib/msvc-x64-md`. No zlib DLL is copied or required.

Windows with Ninja:

```powershell
cmake --preset windows-ninja
cmake --build --preset windows-ninja
```

Linux with Ninja:

```bash
cmake --preset linux-ninja
cmake --build --preset linux-ninja
```

Linux builds use system zlib and minizip development packages. If CMake cannot
find minizip automatically, pass `MINIZIP_INCLUDE_DIR` and `MINIZIP_LIBRARY`.

Third-party notes:
- rapidxml is header-only and bundled under `thirdparty/rapidxml-1.13`.
- Windows/MSVC x64 expects `/MD` static zlib/minizip under
  `thirdparty/zlib-static/lib/msvc-x64-md`.
- Linux expects zlib/minizip from the system or explicit CMake paths.
- Dependency licenses and source details are listed in
  [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
