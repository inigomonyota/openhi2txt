# OpenHi2txt Usage

OpenHi2txt can be used as a command-line tool or as an embedded C++ library.
The library API is the preferred integration point for frontends and other
programs.

## Public Include

```cpp
#include <openhi2txt/openhi2txt.h>
```

All public types live in the `openhi2txt` namespace.

## Version

```cpp
openhi2txt::VersionMajor
openhi2txt::VersionMinor
openhi2txt::VersionPatch
openhi2txt::VersionString
```

## Basic Library Setup

```cpp
openhi2txt::ContextOptions opts;
opts.definitionsZip = "path/to/hi2txt.zip";
opts.defaultsZip = "path/to/hi2txt_defaults.zip";
opts.scoresDirectory = "path/to/scores";
opts.mameRoot = "path/to/mame";

openhi2txt::Context context(opts);

openhi2txt::ReadOptions read;
read.includeExtra = false;
read.includeDebug = false;
read.useDefaultFallback = true;

openhi2txt::HiScoreResult result = context.readGame("galaga", read);
```

`definitionsZip` should point to the ZIP containing hi2txt XML definitions.
`mameRoot` should point to the MAME folder containing `hiscore`, `nvram`,
`diff`, and `roms`, and optionally `plugins/hiscore/hiscore.dat`.
`defaultsZip` and `scoresDirectory` provide persisted, already-rendered display
tables for fast frontend reads.

## Source Priority

`readGame()` is intended for fast frontend display reads. It does not inspect
live `.hi`, NVRAM, or DIF-backed data. It returns the best persisted display source in this
order:

1. Saved rendered score XML under `scoresDirectory`
2. Default rendered score XML from `defaultsZip`

`refreshGame()` is intended for controlled update moments, such as after a user
exits a game. It reads live `.hi`, NVRAM, or DIF-backed data under `mameRoot` using
`definitionsZip`. If decoding succeeds, it returns the live result and writes a
rendered XML snapshot to `scoresDirectory/<game>.xml` when `scoresDirectory` is
set.

The returned source is available as:

```cpp
result.source
```

Possible values:

```cpp
openhi2txt::ScoreSource::RealInput
openhi2txt::ScoreSource::SavedCache
openhi2txt::ScoreSource::DefaultFallback
openhi2txt::ScoreSource::None
```

If live `.hi`, NVRAM, or DIF-backed data exists but cannot be matched or decoded, OpenHi2txt
does not write a saved score XML. The caller can keep its existing in-memory
display entry and call `readGame()` separately if it wants to reload persisted
data.

## Bulk Persisted Loading

For frontend startup, prefer `readAllPersistedGames()` over looping through
`listDefaultGames()` and calling `readGame()` one game at a time:

```cpp
std::unordered_map<std::string, openhi2txt::HiScoreResult> scoreCache =
    context.readAllPersistedGames(read);
```

`readAllPersistedGames()` bulk-loads already-rendered persisted XML only. It
loads default XMLs from `defaultsZip` first, then saved XMLs from
`scoresDirectory`, so saved real-score snapshots override defaults for the same
game. It does not inspect live `.hi` or nvram data and does not write anything
to disk.

The returned map is keyed by game name. Invalid or undecodable persisted XMLs
are skipped instead of failing the whole import. Set
`read.useDefaultFallback = false` if you want the bulk import to ignore
`defaultsZip` and load only saved score XMLs.

## Read-Only Use

For a pure read-only integration, leave `scoresDirectory` empty:

```cpp
openhi2txt::ContextOptions opts;
opts.definitionsZip = "path/to/hi2txt.zip";
opts.mameRoot = "path/to/mame";
opts.defaultsZip = "path/to/hi2txt_defaults.zip";
```

With no `scoresDirectory`, `readGame()` reads only defaults. `refreshGame()`
can still read live `.hi`, NVRAM, or DIF-backed data, but no saved XML writeback is attempted.

## Defaults

Defaults are useful when a frontend wants something to display before the user
has created a real score file.

```cpp
openhi2txt::ContextOptions opts;
opts.definitionsZip = "path/to/hi2txt.zip";
opts.defaultsZip = "path/to/hi2txt_defaults.zip";
opts.mameRoot = "path/to/mame";
```

Defaults are used by `readGame()` when saved score XML is unavailable.
They can be disabled per read:

```cpp
openhi2txt::ReadOptions read;
read.useDefaultFallback = false;
```

## Saved Score XML Cache

Set `scoresDirectory` to enable saved rendered score XMLs:

```cpp
openhi2txt::ContextOptions opts;
opts.definitionsZip = "path/to/hi2txt.zip";
opts.defaultsZip = "path/to/hi2txt_defaults.zip";
opts.scoresDirectory = "path/to/scores";
opts.mameRoot = "path/to/mame";
```

When `refreshGame()` decodes live score data successfully, OpenHi2txt renders
the result to:

```text
scoresDirectory/<game>.xml
```

On later runs, `readGame()` uses that saved XML before falling back to defaults.

OpenHi2txt avoids rewriting the saved XML when the rendered bytes are unchanged.

## Obfuscation

OpenHi2txt supports optional XOR obfuscation for default XMLs and saved score
XMLs. Definition XMLs in `hi2txt.zip` are never obfuscated.

Default XML obfuscation:

```cpp
opts.defaults.obfuscation = openhi2txt::ObfuscationMode::Xor;
opts.defaults.key = "your-key";
```

Saved score XML obfuscation:

```cpp
opts.scoreCache.obfuscation = openhi2txt::ObfuscationMode::Xor;
opts.scoreCache.key = "your-key";
```

If XOR is enabled with an empty key, `readGame()` or `refreshGame()` returns an
error when that obfuscated source is used.

XOR is intended as simple obfuscation, not strong security.

## Startup Cache Pattern

A frontend usually owns its own in-memory display cache. OpenHi2txt resolves the
score data; the frontend decides when to store and display it.

```cpp
openhi2txt::ReadOptions read;
read.includeExtra = false;
read.includeDebug = false;
read.useDefaultFallback = true;

std::unordered_map<std::string, openhi2txt::HiScoreResult> scoreCache =
    context.readAllPersistedGames(read);
```

This is usually much faster than repeatedly opening the defaults ZIP for each
game. Saved score XMLs in `scoresDirectory` override defaults automatically.

After a user exits a game:

```cpp
auto result = context.refreshGame(gameName);
if (result.ok) {
    scoreCache[gameName] = std::move(result);
}
```

For bytes obtained without reading MAME's files, such as a live IPC snapshot,
pass one or more named inputs directly:

```cpp
std::vector<openhi2txt::HiScoreInput> inputs{
    { ".hi", hiscoreBytes, "mame://current-session/hiscore" }
};

auto result = context.decodeGame(gameName, inputs, read);
if (result.ok) {
    scoreCache[gameName] = std::move(result);
}
```

`fileKind` matches the definition's `structure@file`; empty and `hi` are
accepted as aliases for `.hi`. Multiple inputs allow a definition to combine,
for example, `.hi` and NVRAM data through the same merge path used for files.
For `file="dif"`, supply the logical byte window requested by the structure,
not a CHD or DIF container. `decodeGame()` does not write `scoresDirectory`;
the caller controls how frequently live results are cached.

### Planning live watch ranges

Before asking MAME to observe a live source, obtain the conservative watch
plan derived from the resolved XML definition:

```cpp
auto plan = context.planGameInputs(gameName);
if (plan.ok) {
    for (const auto& input : plan.inputs) {
        // input.fileKind identifies the persistent source.
        // input.watchRanges contains the offsets MAME needs to observe.
    }
}
```

There is one `HiScoreInputPlan` for each alternative `<structure>`. Declared
sizes and output IDs are retained so the live integration can select the
appropriate alternative. Each range includes all bytes that can affect output,
including structure checks, loop stop conditions, table placement, sorting,
filtering, referenced formatting operands, and hidden values whose validation
can reject the complete snapshot. Adjacent ranges are coalesced.

For `.hi` files and ordinary EEPROM/SRAM/NVRAM sources, watch only these ranges
but send the complete source image after they stabilize. Pass that image to
`decodeGame()` unchanged. This prevents unrelated persistent-state activity
from delaying updates without requiring sparse-buffer reconstruction.

For DIF/CHD definitions, range offsets are absolute logical-disk offsets and
`sourceWindowOffset`/`sourceWindowLength` identify the XML-defined window.
Unlike normal NVRAM, a disk integration should return only that logical window
or use a future sparse-window transport; it should not transmit the whole disk.

When `refreshGame()` finds a valid score table in file-backed `.hi`, NVRAM, or
DIF data, it updates the saved score XML and returns the decoded data. If not,
no saved XML is written and the frontend can keep its existing cached display
entry.

## Result Shape

`HiScoreResult` contains rendered, frontend-ready data:

```cpp
result.tables
result.fields
result.warnings
```

Failures also expose a stable category in addition to the human-readable
message:

```cpp
if (!result.ok) {
    result.errorKind; // openhi2txt::HiScoreErrorKind
    result.error;
}
```

Available categories are:

```cpp
openhi2txt::HiScoreErrorKind::None
openhi2txt::HiScoreErrorKind::DefinitionNotFound
openhi2txt::HiScoreErrorKind::DefinitionInvalid
openhi2txt::HiScoreErrorKind::StructureNotMatched
openhi2txt::HiScoreErrorKind::OutputNotFound
openhi2txt::HiScoreErrorKind::InputNotFound
openhi2txt::HiScoreErrorKind::InvalidData
openhi2txt::HiScoreErrorKind::ScoreXmlNotFound
openhi2txt::HiScoreErrorKind::ScoreXmlInvalid
openhi2txt::HiScoreErrorKind::ConfigurationError
openhi2txt::HiScoreErrorKind::Unknown
```

Tables preserve multi-page/multi-table output:

```cpp
for (const auto& table : result.tables) {
    table.id;
    table.columns;
    table.rows;
}
```

Fields are single key/value outputs. They are useful for extra or debug data,
such as top-score summaries, checksums, padding bytes, or game settings:

```cpp
for (const auto& field : result.fields) {
    field.id;
    field.value;
}
```

Column and field metadata is also available:

```cpp
table.columnInfo;
field.source;
field.display;
```

Definitions using the OpenHi2txt `column@header="false"` extension retain the
column values but place an empty string in the corresponding
`table.columns` position. The matching `table.columnInfo` entry still exposes
the column ID and source.

Display levels:

```cpp
openhi2txt::DisplayLevel::Always
openhi2txt::DisplayLevel::Extra
openhi2txt::DisplayLevel::Debug
```

## Extra And Debug Output

`ReadOptions` controls whether definitions marked as `extra` or `debug` are
included:

```cpp
openhi2txt::ReadOptions read;
read.includeExtra = true; // similar to hi2txt -ra
read.includeDebug = true; // similar to hi2txt -rd
```

Normal frontend displays usually leave both off.

## Command Line

Short form:

```text
openhi2txt -d hi2txt.zip -m <mame-root> -g <romname> -xml
```

Long forms are also accepted:

```text
openhi2txt --defs hi2txt.zip --mame-root <mame-root> --game <romname> --xml
```

Supported read flags:

```text
-r
-ra
-rd
-xml
-trace
-notrace
```

Original hi2txt-style aliases are accepted:

```text
-descr <path>
-ds <path>
-hiscoredat <path>
-hs <path>
<hi_file_path>
```

OpenHi2txt also keeps its explicit forms:

```text
--defs <path>
--mame-root <path>
--game <romname>
```

Display compatibility flags:

```text
-keep-field <column>
-keep-table-value <column:value>
-keep-first-score <yes|no>
-hide-column <column>
-hide-field <column>
-keep-first-table <yes|no>
-max-lines <integer>
-max-columns <integer>
-score-grouping <yes|no>
-score-grouping-separator <string>
-score-grouping-size <integer>
```

Listing flags:

```text
-l
-location
-h
-v
```

`-trace` prints a step-by-step processing log similar to the original hi2txt:
paths used, XML loading, structure matching, bytes consumed by each structure
element, selected output, and displayed fields. `-rd` also enables trace.
`-notrace` disables trace again. Trace is a CLI feature and is not exposed in
the public library API.
