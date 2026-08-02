# OpenHi2txt Definition XML Reference

This document describes the XML files that tell OpenHi2txt how to decode a
game's high-score data. It is an OpenHi2txt-focused successor to the original
hi2txt XML description reference (reference version 1.9), corrected to match
the behavior of the current C++ implementation.

Definition XML is not the same as the rendered score XML written to the score
cache. Definition XML describes binary input; rendered score XML contains the
already-decoded result.

## A complete minimal definition

The following example reads five rows from `example.hi`. Each row contains a
three-byte BCD score followed by three initials. The score is multiplied by 10
for display, and the loop index supplies the rank.

```xml
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE hi2txt SYSTEM "hi2txt.dtd">
<hi2txt label="Example game">
  <structure file=".hi">
    <check>
      <size>30</size>
    </check>

    <loop count="5">
      <elt id="SCORE" type="int" size="3" decoding-profile="bcd"/>
      <elt id="NAME" type="text" size="3" charset="CS_INITIALS"/>
    </loop>
  </structure>

  <charset id="CS_INITIALS">
    <char src="00" dst="A"/>
    <char src="01" dst="B"/>
    <char src="02" dst="C"/>
    <!-- Add the rest of the game's character map. -->
    <char src="1A" dst=" "/>
  </charset>

  <output>
    <table id="SCORES">
      <column id="RANK" src="index"/>
      <column id="SCORE" format="*10"/>
      <column id="NAME"/>
    </table>
  </output>
</hi2txt>
```

Put the definition in the definitions ZIP as `example.xml`, where `example`
is the MAME short name. With a standard MAME layout, test it with:

```text
openhi2txt --defs hi2txt.zip --mame-root C:\MAME --game example --xml --trace
```

The original hi2txt-compatible command form is also accepted:

```text
openhi2txt -descr hi2txt.zip -hiscoredat C:\MAME\plugins\hiscore\hiscore.dat -r C:\MAME\hi\example.hi -xml -trace
```

`--trace` is especially useful while authoring a definition because it reports
the selected structure, bytes consumed by each element, and selected output.

## Root elements and compatibility

### Legacy-compatible definitions

Use the `hi2txt` root for definitions that use only the shared hi2txt grammar:

```xml
<!DOCTYPE hi2txt SYSTEM "hi2txt.dtd">
<hi2txt label="Example">
  <!-- structures, formats, charsets, bitmasks, and outputs -->
</hi2txt>
```

OpenHi2txt accepts these definitions, and they remain usable by the original
hi2txt program. The optional `label` and `ingame-score` attributes are accepted
as compatibility metadata.

### OpenHi2txt-only definitions

When a definition actually needs an OpenHi2txt extension, replace the root and
declare its minimum required version:

```xml
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE openhi2txt>
<openhi2txt requires="0.2.0" label="Example">
  <!-- definition -->
</openhi2txt>
```

`requires` must contain exactly three non-negative numeric components:
`MAJOR.MINOR.PATCH`. Leading zeroes such as `01.0.0`, missing components, and
suffixes such as `1.0.0-beta` are rejected. OpenHi2txt refuses to load a
definition requiring a newer version than the running library.

The `openhi2txt` root supports the same attributes and children as `hi2txt`,
plus the required `requires` attribute and explicitly documented extensions.
Version 0.2.0 introduces `loop@stop-when` and
`column@source-row="output_index"`. Existing definitions should keep the legacy
root until they actually use an extension. The root alone never changes
extraction, filtering, sorting, or rendering behavior.

The DOCTYPE is a compatibility marker, not a schema source. OpenHi2txt uses
RapidXML to parse the document and validates the supported elements and
attributes in its own code. It does not load, resolve, or validate a DTD.

## Top-level organization

Both roots accept these child elements:

| Element | Purpose |
| --- | --- |
| `structure` | Selects an input file/layout and decodes binary values. |
| `format` | Declares a reusable value transformation. |
| `charset` | Maps game-specific byte values to text. |
| `bitmask` | Extracts characters from selected source bits. |
| `output` | Selects and arranges fields and tables for display. |
| `sameas` | Reuses another game's definition. |

Definitions are conventionally ordered as structures, bitmasks, formats,
charsets, then outputs, although OpenHi2txt does not require that order.

## Reusing another definition

For a clone with an identical layout, the entire definition may be:

```xml
<!DOCTYPE hi2txt SYSTEM "hi2txt.dtd">
<hi2txt>
  <sameas id="parentgame"/>
</hi2txt>
```

The text form is also accepted:

```xml
<sameas>parentgame</sameas>
```

The resolver follows a chain of aliases, with a maximum of 16 hops. Use the
MAME short name of the target definition.

## Structures

A `structure` describes one possible binary layout. OpenHi2txt tries structures
in document order and uses the first one whose file and checks match.

```xml
<structure file=".hi" output="DEFAULT" byte-swap="2">
  <check>...</check>
  <elt .../>
  <loop ...>...</loop>
</structure>
```

| Attribute | Meaning |
| --- | --- |
| `file` | `.hi` by default, or another file relative to the game's data location, commonly an NVRAM file name. |
| `output` | ID of the `output` to render. Omit it to select the unnamed output. |
| `byte-swap` | Swaps fixed-size chunks across the entire selected input before decoding. |

### File-size checks

Use one or more `size` children to list acceptable sizes. Multiple sizes are
alternatives, not cumulative requirements.

```xml
<check>
  <size>120</size>
  <size>128</size>
</check>
```

### hiscore.dat location checks

A `definition` without an `offset` contains a location token from the game's
`hiscore.dat` entry. This helps distinguish layouts that may have the same file
size.

```xml
<check>
  <definition>maincpu:20C0:0040</definition>
</check>
```

Commas and colons are normalized, so the corresponding comma-separated form
is accepted. Tokens are matched against the selected game's current
`hiscore.dat` definition.

### Exact byte-signature checks

With `offset`, a `definition` checks literal hexadecimal bytes in the input:

```xml
<check>
  <size>64</size>
  <definition offset="0">48 49</definition>
  <definition offset="62">AA 55</definition>
</check>
```

All byte signatures in the check must match. Offsets are zero-based.

### Alternative layouts and outputs

Multiple structures are useful when game revisions or emulators store the same
scores differently:

```xml
<structure file=".hi" output="HI_LAYOUT">
  <check><size>40</size></check>
  <!-- .hi decoding -->
</structure>

<structure file="scores.dat" output="NVRAM_LAYOUT">
  <check><size>256</size></check>
  <!-- NVRAM decoding -->
</structure>

<output id="HI_LAYOUT">...</output>
<output id="NVRAM_LAYOUT">...</output>
```

## Binary elements

An `elt` consumes bytes and stores one decoded value in the current row:

```xml
<elt id="SCORE" type="int" size="4" endianness="big_endian" format="*10"/>
```

### Core attributes

| Attribute | Meaning |
| --- | --- |
| `id` | Name used by output columns, fields, formats, and table indexing. |
| `type` | `int`, `text`, or `raw`. |
| `size` | Number of source bytes consumed before transformations. |
| `offset` | Absolute, zero-based input position. When omitted, reading continues at the cursor. |
| `format` | Reusable format ID or inline format chain applied after decoding. |
| `endianness` | `big_endian` (default) or `little_endian`. |
| `decoding-profile` | Preset for common encodings: `bcd`, `bcd-le`, `base-32`, or `base-40`. |
| `base` | Integer or packed-text numeric base; see below. |
| `charset` | Charset chain for text decoding. |

An element outside a loop becomes a value available to every output row. An
element inside a loop becomes a per-row value.

### Integer decoding

Plain integers are big-endian unless `endianness="little_endian"` is set:

```xml
<elt id="SCORE" type="int" size="4" endianness="little_endian"/>
```

For compatibility with existing definitions, `base="16"` means packed BCD,
not a conventional hexadecimal integer:

```xml
<!-- Packed bytes 12 34 56 become decimal 123456. -->
<elt id="SCORE" type="int" size="3" base="16"/>
```

The `bcd` decoding profile is different: it is intended for data storing one
BCD digit in the low nibble of each byte. It supplies `nibble-skip="odd"` when
that attribute is not given explicitly:

```xml
<!-- Bytes 01 02 03 become decimal 123. -->
<elt id="SCORE" type="int" size="3" decoding-profile="bcd"/>
```

Use `base="hex"` or `base="hexa"` when the bytes represent an ordinary binary
integer. `decoding-profile="bcd-le"` is the little-endian, one-digit-per-byte
profile; `base="bcd-le"` handles little-endian packed BCD. Other numeric bases
greater than one are supported for definitions that encode a number as digits
in that base.

### Text decoding and charsets

Without a custom charset, text bytes are decoded as characters after the
configured numeric adjustments. A custom charset maps byte values explicitly:

```xml
<charset id="GAME_CHARS">
  <char src="00" dst="0"/>
  <char src="01" dst="1"/>
  <char src="10" dst="A"/>
  <char src="11" dst="B"/>
  <char src="24" dst=" "/>
</charset>

<structure>
  <elt id="NAME" type="text" size="3" charset="GAME_CHARS"/>
</structure>
```

`char@src` accepts hexadecimal byte values; two-character values such as `10`
are interpreted as hexadecimal. When duplicate source values occur, the first
mapping wins.

Charset IDs may be chained with semicolons and are applied from right to left:

```xml
<elt id="NAME" type="text" size="3" charset="GAME_CHARS;CS_NUMBER"/>
```

The built-in `CS_NUMBER` charset converts numeric values to character codes.
It accepts optional offset and step parameters:

```xml
charset="CS_NUMBER[48]"
charset="CS_NUMBER[48,1]"
```

`ascii-step` divides a source value before `ascii-offset` is added:

```xml
<elt id="LETTER" type="text" size="1" ascii-step="2" ascii-offset="65"/>
```

Use UTF-8 text or numeric XML character references for non-ASCII output. Since
OpenHi2txt does not load the DTD, custom DTD entity declarations are not a
portable way to supply characters.

### Packed base-32 and base-40 text

The profiles provide defaults suitable for common packed-name encodings:

```xml
<elt id="NAME" type="text" size="2" decoding-profile="base-40"/>
```

`base-32` and `base-40` default to a 16-bit source unit, three destination
characters, and an ASCII offset of 64. Explicit `src-unit-size`,
`dst-unit-size`, and `ascii-offset` attributes override those defaults.

### Byte, nibble, and bit transformations

OpenHi2txt supports these transformations on an element:

| Attribute | Effect |
| --- | --- |
| `byte-skip` | Keeps selected bytes. `odd`, `even`, a repeating binary pattern, and a sentinel byte such as `0xFF` are supported. |
| `byte-trim` | Removes matching leading bytes. |
| `byte-trunc` | Stops at the first matching byte. |
| `byte-swap` | Reverses each fixed-size byte group. |
| `nibble-skip` | `odd` keeps low nibbles; `even` keeps high nibbles. |
| `nibble-trim` | Removes matching leading nibbles. |
| `bit-swap` | Reverses the bit order within every byte when true. |
| `bitmask` | Applies a named top-level bitmask. |

Examples:

```xml
<!-- Keep bytes 0 and 2 from each repeating four-byte group. -->
<elt id="VALUE" type="raw" size="8" byte-skip="1010"/>

<!-- Ignore leading FF bytes and stop at the first 00 byte. -->
<elt id="NAME" type="text" size="16" byte-trim="0xFF" byte-trunc="0x00"/>

<!-- Read the low nibble from every byte. -->
<elt id="DIGITS" type="int" size="4" nibble-skip="odd"/>
```

Unless overridden, transformations run in this order:

1. `byte-skip`
2. `endianness`
3. `byte-trim`
4. `byte-trunc`
5. `byte-swap`
6. `nibble-skip`
7. `nibble-trim`
8. `bit-swap`
9. `bitmask`

`swap-skip-order` replaces the default order with a comma- or
semicolon-separated list when a definition needs a different sequence:

```xml
<elt id="VALUE" type="int" size="4"
     byte-swap="2" byte-skip="odd"
     swap-skip-order="byte-swap,byte-skip"/>
```

### Bitmasks

A top-level bitmask defines one mask per output character. Set bits identify
the source bits used for that character; character masks are concatenated in
declaration order.

```xml
<bitmask id="TWO_DIGITS" byte-completion="true">
  <character mask="11110000"/>
  <character mask="00001111"/>
</bitmask>

<structure>
  <elt id="DIGITS" type="text" size="1"
       bitmask="TWO_DIGITS" charset="CS_NUMBER[48]"/>
</structure>
```

Whitespace in a `mask` is ignored. With `byte-completion="true"` (the default),
partial results are padded to full bytes.

## Loops

A loop repeats its `elt` children and creates table rows:

```xml
<loop count="10" start="1" step="1"
      skip-first-bytes="2" skip-last-bytes="1">
  <elt id="SCORE" type="int" size="4"/>
  <elt id="NAME" type="text" size="3"/>
</loop>
```

| Attribute | Default | Meaning |
| --- | --- | --- |
| `count` | `1` | Number of iterations. |
| `start` | `0` | First logical loop index. |
| `step` | `1` | Amount added to the logical index per iteration. |
| `skip-first-bytes` | `0` | Bytes skipped before each iteration's elements. |
| `skip-last-bytes` | `0` | Bytes skipped after each iteration's elements. |

### Terminated loops (OpenHi2txt 0.2.0)

Some files contain a variable-length record history followed by unrelated
data. Under the `openhi2txt` root, `stop-when="ID:value"` stops a loop when the
named element in an iteration has the specified decoded value:

```xml
<loop count="2025" start="10" stop-when="SCORE:0">
  <elt id="SCORE" type="int" size="4" base="16"/>
</loop>
```

OpenHi2txt reads and decodes the entire fixed-size iteration before testing the
condition. If it matches, the iteration's bytes are consumed, none of its
values are committed to logical rows, and the loop ends. Earlier iterations
remain committed. A nonmatching iteration is committed normally.

The comparison is against the complete decoded value, not individual source
bytes. Consequently, packed BCD `00 01 96 00` decodes to `19600` and is kept;
only a complete record decoding to zero matches `SCORE:0`.

A terminated loop must declare an explicit positive `count`, which remains its
safety ceiling when no terminator occurs. `stop-when` is rejected under the
legacy `hi2txt` root. Without it, loop extraction is unchanged.

The logical index is available to `table-index`, `src="index"`, and the
`LoopIndex` inline formatter.

## Table indexing

`table-index` controls which logical row receives an element. If omitted, the
current loop index is used.

| Value | Use |
| --- | --- |
| `loop_index` | Current logical loop index. |
| `loop_reverse_index` | Reverse loop index. |
| `last` | Last row. |
| `itself` | Use the element's own decoded value as its row index. |
| An integer | Fixed row index. |
| `COLUMN:index_from_value` | Derive an index from the named column's value. |
| `COLUMN:value_from_index` | Derive the value from the named column's index. |

Apply a format while deriving the index with `table-index-format`:

```xml
<elt id="RANKED_SCORE" type="int" size="3"
     table-index="RANK:index_from_value"
     table-index-format="-1"/>
```

This is useful when ranks, scores, and names are stored in separate blocks or
in a different order.

## Outputs

An output arranges decoded values without consuming any more input:

```xml
<output>
  <field id="TOP SCORE" src="BEST" format="*10"/>
  <table id="HIGH SCORES">
    <column id="RANK" src="index"/>
    <column id="SCORE" format="*10"/>
    <column id="NAME"/>
  </table>
</output>
```

An unnamed `output` is the default. A named output is selected by
`structure@output`.

### Fields

A top-level `field` displays one scalar value. This is the way to expose data
such as the in-game top-score field shown separately from the high-score table:

```xml
<structure>
  <elt id="BEST" type="int" size="3" decoding-profile="bcd"/>
  <!-- table data follows -->
</structure>

<output>
  <field id="TOP SCORE" src="BEST" format="*10"/>
  <table id="SCORES">...</table>
</output>
```

If `src` is omitted, it defaults to the field's `id`. If `id` is omitted, it
defaults to `src`.

### Tables and columns

Columns select values from each decoded row:

```xml
<table id="SCORES" sort="SCORE" sort-order="descending" lines-max="10">
  <column id="RANK" src="index"/>
  <column id="SCORE" format="*10"/>
  <column id="PLAYER" src="NAME" format="Trim "/>
</table>
```

As with fields, `id` and `src` fall back to each other. Special sources are:

- `index`: the row's displayed index.
- `unsorted_index`: the index before output sorting.

Table attributes:

| Attribute | Meaning |
| --- | --- |
| `sort` | Column used as the sort key. |
| `sort-order` | Ascending or descending order. |
| `sort-format` | Format applied to the sort key before comparison. |
| `lines-max` | Maximum number of rows after filtering and sorting. |
| `line-ignore` | One or more `COLUMN:VALUE` rules, separated by commas or semicolons. |
| `line-ignore-operator` | `and` (default), `or`, or a comparison operator used by the rules. |
| `display` | `always`, `extra`, or `debug`. |

For example, omit empty factory rows:

```xml
<table id="SCORES" line-ignore="SCORE:0;NAME:AAA"
       line-ignore-operator="and">
  ...
</table>
```

`display="extra"` requires `-ra` or `-rd`; `display="debug"` requires `-rd`.
The default and `always` are shown by `-r`.

Fields and columns accept the same `display` values.

### Positional source rows (OpenHi2txt 0.2.0)

Ordinarily, every column reads from the current candidate row after filtering
and sorting. A positional column can instead read its named value from the
original logical row matching the final output position:

```xml
<table sort="SCORE" sort-order="desc" lines-max="10">
  <column id="RANK" src="index" format="+1"/>
  <column id="NAME" source-row="output_index"/>
  <column id="SCORE"/>
</table>
```

For final output row `i`, `NAME` is read from original logical row `i`, while
`SCORE` continues to come from the sorted candidate row. The lookup occurs
during cell rendering; values are not copied between extraction rows before
sorting.

A positional column is not used to decide whether the current candidate row
belongs to the table. In this example, `SCORE` supplies that evidence, allowing
score-only candidate rows to participate in sorting even when they have no
`NAME` value.

`source-row` is rejected under the legacy root. Omitting it preserves the
ordinary current-row lookup exactly.

## Formats

A format transforms an element, field, column, index, or referenced value. A
format expression may be a named format, an inline shorthand, or a chain
separated by `;` or `|`.

### Inline shorthand

```xml
<column id="SCORE" format="*10"/>
<column id="SECONDS" format="/60;Round"/>
<column id="NAME" format="Trim ;Uppercase"/>
<column id="RANK" src="index" format="+1;Prefix#"/>
```

Supported shorthand includes:

| Token | Effect |
| --- | --- |
| `+N`, `-N` | Add or subtract an integer. |
| `*N`, `xN`, `XN` | Multiply by an integer. |
| `/N` | Divide and retain a fractional result. |
| `%N` | Integer remainder. |
| `dN` | Truncating integer division. |
| `DN` | Rounded integer division. |
| `>N` | Logical right shift by N bits. |
| `R`, `Round` | Round to the nearest integer. |
| `T`, `Trunc` | Truncate to an integer. |
| `LC`, `Lowercase` | Convert to lowercase. |
| `UC`, `Uppercase` | Convert to uppercase. |
| `Capitalize` | Capitalize text. |
| `TrimLx`, `TrimRx`, `Trimx` | Trim characters from the left, right, or both sides. |
| `PadL50` | Pad left to width 5 with `0`; `PadR` is the right-hand form. |
| `Prefixtext`, `Suffixtext` | Add literal text before or after the value. |
| `LoopIndex` | Replace the input with the current logical loop index. |
| `hex`, `hexadecimal_string`, `0x` | Produce a `0x`-prefixed hexadecimal string. |

Whitespace after `Trim`, `TrimL`, or `TrimR` is significant when the character
to trim is a space.

### Named formats

Named formats make longer operations reusable:

```xml
<format id="SCORE_DISPLAY">
  <multiply>10</multiply>
  <pad direction="left" max="8" value="0"/>
</format>

<format id="TIME_DISPLAY" formatter="%.2f sec">
  <divide>60</divide>
</format>

<output>
  <field id="BEST" format="SCORE_DISPLAY"/>
  <table>
    <column id="TIME" format="TIME_DISPLAY"/>
  </table>
</output>
```

`formatter` is a C/C++ `printf`-style numeric formatter. Integer conversions
such as `%d`, `%x`, and `%X`, floating conversions such as `%.2f`, literal text,
and `%%` are supported. It is not a Java `Formatter` expression.

Set `apply-to="char"` to apply a format independently to each UTF-8 character;
the default is `value`.

### Long-form operations

The following children are accepted in a `format`:

- Arithmetic: `add`/`increment`, `substract`/`decrement`, `multiply`, `divide`,
  `remainder`, `divide_trunc`, `divide_round`, `shift`, `round`, and `trunc`.
  The historic spelling `substract` is intentional. Long-form `shift` shifts
  left; the inline `>N` shorthand shifts right.
- Aggregation: `sum`, `min`, `max`, and `concat`.
- Text: `prefix`, `suffix`/`postfix`, `pad`, `trim`, `replace`, `uppercase`,
  `lowercase`, `capitalize`, and `case`.

Arithmetic operands may be literals:

```xml
<format id="BONUS_SCORE">
  <multiply>100</multiply>
  <add>5000</add>
</format>
```

Or references to another field or column:

```xml
<format id="TOTAL">
  <add><column id="BONUS"/></add>
</format>
```

Referenced `field` and `column` elements may themselves have a `format`.

Aggregation example:

```xml
<format id="FULL_NAME" input-as-subcolumns-input="true">
  <concat>
    <column id="FIRST"/>
    <txt> </txt>
    <column id="LAST"/>
  </concat>
</format>
```

`concat` accepts `column`, `field`, and `txt` children. Literal text in ordinary
output `field` or `column` elements is not an input source; use `concat` for
composed text.

Conditional mapping example:

```xml
<format id="DIFFICULTY_NAME">
  <case src="0" dst="EASY"/>
  <case src="1" dst="NORMAL"/>
  <case src="2" dst="HARD"/>
  <case default="true" dst="UNKNOWN"/>
</format>
```

`case` also accepts comparison operators in `operator`, a preprocessing chain
in `operator-format`, and an output format in `format` when `dst` is omitted.

## Practical patterns

### NVRAM-backed scores

Set `structure@file` to the file within the game's NVRAM directory:

```xml
<structure file="scores.dat">
  <check>
    <size>512</size>
    <definition offset="0">53 43 4F 52 45</definition>
  </check>
  <loop count="10" skip-first-bytes="2">
    <elt id="SCORE" type="int" size="4" endianness="little_endian"/>
    <elt id="NAME" type="text" size="3" charset="GAME_CHARS"/>
  </loop>
</structure>
```

The exact path resolution depends on whether the CLI is given a MAME root or a
direct input path. Use `--trace` to confirm the selected file.

### Separate blocks for names and scores

Use separate loops with matching logical indices when a file stores columns in
contiguous blocks:

```xml
<structure>
  <loop count="5">
    <elt id="SCORE" type="int" size="3"
         table-index="loop_index" decoding-profile="bcd"/>
  </loop>
  <loop count="5">
    <elt id="NAME" type="text" size="3"
         table-index="loop_index" charset="GAME_CHARS"/>
  </loop>
</structure>
```

The first loop consumes the 15-byte score block, so the second begins at the
name block and advances normally. An absolute `offset` always addresses the
same position whenever that particular element runs, so do not place one on an
element that must advance within a loop.

### Showing a field omitted by default

Decoding an `elt` does not automatically display it. Add it to the selected
output as a top-level field:

```xml
<elt id="STAGE" type="int" size="1"/>

<output>
  <field id="STAGE"/>
  <table id="SCORES">...</table>
</output>
```

If it is marked `display="extra"`, invoke OpenHi2txt with `-ra`; if it is
`display="debug"`, use `-rd`.

### Combining the extensions for separated ranked labels and score history

The two 0.2.0 extensions are designed to work together for binary layouts in
which:

- a fixed block of names or labels is already stored in final displayed order;
- an initial block of score candidates is followed by an appended history;
- the history ends at a sentinel record rather than filling the remaining file;
- candidates must be sorted and limited before the final-rank labels are
  attached.

The following generic definition illustrates that layout. The byte counts and
record limit are examples and should be adjusted to the actual binary format:

```xml
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE openhi2txt>
<openhi2txt requires="0.2.0" label="Separated rank data">
  <structure file="records.bin">
    <elt size="16" type="raw" id="HEADER"/>
    <loop count="10">
      <elt size="3" type="text" id="NAME" table-index="loop_index"/>
    </loop>
    <elt size="8" type="raw" id="SKIP TO SCORES"/>

    <loop count="10">
      <elt size="4" type="int" id="SCORE"
           base="16" table-index="loop_index"/>
    </loop>
    <loop count="1000" start="10" stop-when="SCORE:0">
      <elt size="4" type="int" id="SCORE"
           base="16" table-index="loop_index"/>
    </loop>
  </structure>

  <output>
    <table sort="SCORE" sort-order="desc" lines-max="10">
      <column id="RANK" src="index" format="+1"/>
      <column id="NAME" source-row="output_index"/>
      <column id="SCORE"/>
    </table>
  </output>
</openhi2txt>
```

In this example, the first score loop writes baseline candidates to logical
rows 0 through 9. The terminated loop appends history candidates beginning at
row 10. Sorting uses the current candidate row's `SCORE`; only after sorting
does `source-row="output_index"` obtain `NAME` from original logical rows 0
through 9.

`base="16"` is appropriate when each four-byte score is packed BCD. It is not
part of either extension and should be replaced with the encoding attributes
required by the actual data. The legacy `decoding-profile="bcd"` profile has a
different meaning: one low-nibble digit per byte.

## Validation and troubleshooting

OpenHi2txt rejects undeclared elements, undeclared attributes, and elements in
invalid parent locations. Common authoring failures are:

- The XML filename does not match the MAME short name.
- No structure's input file and checks match.
- The decoded value has an `id`, but no output field or column references it.
- A structure selects a named output that does not exist.
- A custom charset omits values that occur in the file.
- A transformation runs in the wrong order; use `swap-skip-order`.
- An `openhi2txt` root omits `requires` or asks for a newer version.
- A definition relies on a DTD declaration or entity that RapidXML does not
  load.

Start with `--trace`, reduce checks temporarily to identify the matching
structure, and add transformations one at a time. Binary inspection is easiest
when the definition temporarily decodes uncertain bytes as `type="raw"` with
`format="hex"` and exposes the value through an output field or column.

## Supported grammar summary

```text
hi2txt | openhi2txt
├── structure
│   ├── check
│   │   ├── definition
│   │   └── size
│   ├── elt
│   └── loop
│       └── elt
├── format
│   ├── arithmetic and text operations
│   └── field/column/txt references where applicable
├── charset
│   └── char
├── bitmask
│   └── character
├── output
│   ├── field
│   └── table
│       ├── column
│       └── field
└── sameas
```
