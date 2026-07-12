# OpenHi2txt

OpenHi2txt is an open-source C++ high-score reader compatible with hi2txt XML
definitions. It is both:

- an embeddable C++ library for frontends, launchers, tools, and scrapers
- an optional command-line executable for hi2txt-compatible CLI workflows

Current version: `0.1.0`

Build: CMake with Visual Studio 2022 (v143) x64, Ninja, or another C++17 compiler.

Detailed library and CLI usage is documented in [docs/usage.md](docs/usage.md).

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

