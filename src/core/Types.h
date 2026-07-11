#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <variant>

namespace openhi2txt {

enum class Endianness { Little, Big };
enum class TrimDir { None, Left, Right, Both };
enum class FormatKind {
    Add,
    Substract,
    Multiply,
    Divide,        // float output
    Remainder,     // int modulo
    DivideTrunc,   // int division
    DivideRound,   // rounded int division
    Shift          // logical left shift
};

enum class ApplyToKind { Value, Char };

struct CaseMap {
    std::string src;
    std::string dst;
    bool isDefault = false;
    bool hasDst = false;
    std::string op;              // <, ==, >, >=, <=, != (default ==)
    std::string operatorFormat;  // format chain applied before match
    std::string format;          // format chain applied to produce output when dst not set
};
struct TrimOp { TrimDir dir = TrimDir::None; std::string chars; };

struct FormatColRef { std::string id; std::string format; };

struct PadOp {
    bool enabled = false;
    TrimDir dir = TrimDir::Left;
    int max = 0;
    std::string padChar;
};

struct ReplaceOp {
    bool enabled = false;
    std::string src;
    std::string dst;
    bool all = true;
};

enum class ConcatPartKind { Column, Input, Text };

struct ConcatPart {
    ConcatPartKind kind = ConcatPartKind::Column;
    std::string id;       // for Column
    std::string format;   // optional
    std::string text;     // for Text
};

struct AffixOp {
    std::string emptyValue;   // @empty (if set)
    bool hasEmpty = false;
    bool consume = false;     // @consume
    std::vector<ConcatPart> parts; // mixed text + column/field refs
};

struct FormatDef {
    std::string id;

    // spec: @formatter, @apply-to, @input-as-subcolumns-input
    std::string formatter;                 // printf-style subset (%.2f, %d, %s)
    ApplyToKind applyTo = ApplyToKind::Value;
    bool inputAsSubcolumnsInput = false;   // parsed, not used unless you implement that feature

    // numeric ops (in sequence)
    std::vector<std::pair<FormatKind, double>> mathOps;

    // string ops
    std::vector<AffixOp> prefixes;
    std::vector<AffixOp> suffixes;
    std::vector<CaseMap> cases;
    TrimOp trim;
    std::vector<TrimOp> trims;
    PadOp pad;
    ReplaceOp repl;
    std::vector<ReplaceOp> repls;

    bool lowercase = false;
    bool uppercase = false;
    bool capitalize = false;

    bool doRound = false;   // <round/>
    bool doTrunc = false;   // <trunc/>
    bool doLoopIndex = false; // <loopindex/>

    std::vector<FormatColRef> sumCols;
    std::vector<ConcatPart> concatParts;
    std::vector<FormatColRef> minCols;
    std::vector<FormatColRef> maxCols;
    std::vector<std::string> referencedColumns; // ids used by <sum>/<concat>
};

struct Charset {
    std::string id;
    std::unordered_map<uint32_t, std::string> entries;
    std::string dst[256];
    bool used[256]{};
};

enum class TableIndexKind {
    None,
    Fixed,
    LoopIndex,
    LoopReverseIndex,
    Last,
    Itself,
    IndexFromValue,
    ValueFromIndex
};

enum class IntBaseKind { Decimal, Hex, BcdBE, BcdLE };

struct Elt {
    std::string id, type, charsetId;

    std::string format;

    int size = 0;
    int offset = -1;

    Endianness endian = Endianness::Big;

    std::string bytePick; // e.g. "0,2" or "0-1,3" (select/reorder)
    std::string byteSkip;
    std::string byteTrim;
    std::string byteTrunc;
    std::string nibbleSkip;
    std::string nibbleTrim;
    int byteSwap = 0;

    bool bitSwap = false;
    std::string bitmaskId;

    std::string decodingProfile; // e.g. "bcd"

    int srcUnitSizeBits = 0;
    int dstUnitCount = 0;

    std::string swapSkipOrder;

    IntBaseKind intBase = IntBaseKind::Decimal;
    int numericBase = 0;        // generic int base conversion, e.g. base="256"

    // For base-32/base-40 packed text decoding (decoding-profile base-32/base-40)
    int textBase = 0;          // 0 = disabled, else 32 or 40
    bool hasTextBase = false;

    int asciiOffset = 0;
    bool hasAsciiOffset = false;

    int asciiStep = 1;
    bool hasAsciiStep = false;

    TableIndexKind tableIndexKind = TableIndexKind::None;
    int tableIndexFixed = 0;
    std::string tableIndexCol;
    std::string tableIndexFormat;
};

struct Loop {
    int count = 0;

    int startIndex = 0;
    bool hasStart = false;

    int step = 1;
    bool hasStep = false;

    int skipFirstBytes = 0;
    int skipLastBytes = 0;

    std::vector<Elt> elts;
};

struct StructureItem {
    enum class Kind { Elt, Loop } kind = Kind::Elt;
    Elt elt;
    Loop loop;
};

struct CheckDef {
    int offset = 0;
    std::vector<uint8_t> bytes;
};

struct Structure {
    std::string fileKind = ".hi";
    int byteSwap = 0;
    std::vector<int> checkSizes;

    std::string outputId; // "" = default <output> (no id), else matches <output id="...">

    std::vector<CheckDef> checkAll; // AND
    std::vector<CheckDef> checkAny; // OR (legacy inline)
    std::vector<std::string> hiscoreDefinitionTokens;
    std::vector<StructureItem> items;
};

struct Column { std::string id, src, format, display; };
struct IgnoreRule { std::string colId; std::string value; };
enum class IgnoreOp { And, Or };

struct Table {
    std::string id;
    std::string display; // always|extra|debug
    std::string lineIgnoreRaw;
    std::vector<IgnoreRule> ignoreRules;
    IgnoreOp ignoreOp = IgnoreOp::And;
    std::string ignoreCompareOp;

    std::string sortKey;
    std::string sortOrder;
    std::string sortFormat;

    int linesMax = 0;

    std::vector<Column> cols;
};

struct BitmaskDef {
    std::string id;
    bool byteCompletion = true; // bitmask@byte-completion

    // One mask per output character (order matters).
    // Each mask is a byte-array (same width as the source slice), with bits set where to read.
    std::vector<std::vector<uint8_t>> charMasks;

    // Optional merged mask for legacy/non-text uses (keeps your old behavior available).
    std::vector<uint8_t> mergedMask;
};

struct OutputDef {
    std::string id;            // "" = default
    std::vector<Table> tables; // tables inside that <output>
    std::vector<Column> fields;
};

struct GameDef {
    std::vector<Structure> structures;
    std::unordered_map<std::string, FormatDef> formats;
    std::unordered_map<std::string, Charset> charsets;
    std::unordered_map<std::string, BitmaskDef> bitmasks;

    // key: output id ("" for default)
    std::unordered_map<std::string, OutputDef> outputs;
    std::vector<std::string> outputOrder;
};

using RawBytes = std::vector<uint8_t>;
using Value = std::variant<std::monostate, int64_t, std::string, RawBytes>;

} // namespace openhi2txt
