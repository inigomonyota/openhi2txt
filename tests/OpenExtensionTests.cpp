#include "app/OutputPrinter.h"
#include "app/InputProcessor.h"
#include "core/Processor.h"
#include "core/ResultRenderer.h"
#include "io/Utils.h"
#include "openhi2txt/openhi2txt.h"
#include "xml/XmlParser.h"

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

namespace {

int failures = 0;

void expect(bool condition, const std::string& message) {
    if (condition) return;
    std::cerr << "FAILED: " << message << '\n';
    ++failures;
}

std::string legacyDefinition(const std::string& body) {
    return "<!DOCTYPE hi2txt SYSTEM \"hi2txt.dtd\"><hi2txt>" + body + "</hi2txt>";
}

std::string openDefinition(const std::string& body) {
    return "<!DOCTYPE openhi2txt><openhi2txt requires=\"" +
        std::string(openhi2txt::VersionString) + "\">" + body + "</openhi2txt>";
}

openhi2txt::GameDef parseDefinition(const std::string& xml, const std::string& message) {
    const auto parsed = openhi2txt::XmlParser::parseWithDiagnostics(xml);
    expect(parsed.ok, message + (parsed.error.empty() ? "" : ": " + parsed.error));
    return parsed.def;
}

std::string printed(const openhi2txt::HiScoreResult& result, openhi2txt::OutputFormat format) {
    std::fflush(stdout);
    std::FILE* temporary = std::tmpfile();
    if (!temporary) return {};

#ifdef _WIN32
    const int saved = _dup(_fileno(stdout));
    _dup2(_fileno(temporary), _fileno(stdout));
#else
    const int saved = dup(fileno(stdout));
    dup2(fileno(temporary), fileno(stdout));
#endif

    openhi2txt::OutputPrinter::print(result, format);
    std::fflush(stdout);

#ifdef _WIN32
    _dup2(saved, _fileno(stdout));
    _close(saved);
#else
    dup2(saved, fileno(stdout));
    close(saved);
#endif

    std::rewind(temporary);
    std::string output;
    char buffer[1024];
    while (const size_t count = std::fread(buffer, 1, sizeof(buffer), temporary)) {
        output.append(buffer, count);
    }
    std::fclose(temporary);
    return output;
}

int64_t valueAt(const std::vector<std::unordered_map<std::string, openhi2txt::Value>>& rows,
                size_t row,
                const std::string& id) {
    if (row >= rows.size()) return -1;
    const auto found = openhi2txt::Utils::findIdentifier(rows[row], id);
    return found == rows[row].end() ? -1 : openhi2txt::Utils::valueToInt(found->second);
}

void testRootParityAndOrdinaryRegressions() {
    using namespace openhi2txt;

    const std::string body =
        "<structure>"
        "<loop count=\"3\"><elt id=\"SCORE\" type=\"int\" size=\"1\"/></loop>"
        "</structure>"
        "<output><table sort=\"SCORE\" sort-order=\"desc\" lines-max=\"2\">"
        "<column id=\"RANK\" src=\"index\" format=\"+1\"/>"
        "<column id=\"SCORE\"/>"
        "</table></output>";

    const GameDef legacy = parseDefinition(legacyDefinition(body), "legacy parity definition parses");
    const GameDef open = parseDefinition(openDefinition(body), "open parity definition parses");
    expect(legacy.structures.size() == 1 && open.structures.size() == 1,
        "both roots parse the same structure count");
    expect(!legacy.structures[0].items[0].loop.hasStopCondition &&
           !open.structures[0].items[0].loop.hasStopCondition,
        "ordinary loops remain non-terminated under both roots");

    const std::vector<uint8_t> raw{ 2, 5, 3 };
    const auto legacyRows = Processor::extractRows(raw, legacy.structures[0], legacy);
    const auto openRows = Processor::extractRows(raw, open.structures[0], open);
    expect(legacyRows == openRows, "legacy-only extraction is identical under both roots");
    expect(legacyRows.size() == 3 && valueAt(legacyRows, 0, "SCORE") == 2 &&
        valueAt(legacyRows, 1, "SCORE") == 5 && valueAt(legacyRows, 2, "SCORE") == 3,
        "ordinary loop extraction retains its existing row behavior");

    const ReadOptions options;
    const auto legacyResult = ResultRenderer::render(legacy, legacyRows, "", options);
    const auto openResult = ResultRenderer::render(open, openRows, "", options);
    expect(legacyResult.tables.size() == 1 && openResult.tables.size() == 1,
        "both roots render one table");
    expect(legacyResult.tables[0].rows == openResult.tables[0].rows,
        "legacy-only rendering is identical under both roots");
    expect(legacyResult.tables[0].rows[0][1] == "5" &&
        legacyResult.tables[0].rows[1][1] == "3",
        "ordinary columns still read the current sorted row");
    const std::string legacyText = printed(legacyResult, OutputFormat::Text);
    const std::string openText = printed(openResult, OutputFormat::Text);
    expect(!legacyText.empty() && legacyText == openText,
        "text output is identical under both roots");
    const std::string legacyXml = printed(legacyResult, OutputFormat::Xml);
    const std::string openXml = printed(openResult, OutputFormat::Xml);
    expect(!legacyXml.empty() && legacyXml == openXml,
        "XML output is identical under both roots");
}

void testRootAwareValidation() {
    using namespace openhi2txt;

    const std::string stopBody =
        "<structure><loop count=\"2\" stop-when=\"SCORE:0\">"
        "<elt id=\"SCORE\" type=\"int\" size=\"1\"/>"
        "</loop></structure>";
    auto parsed = XmlParser::parseWithDiagnostics(legacyDefinition(stopBody));
    expect(!parsed.ok && parsed.errorKind == XmlParseErrorKind::Schema &&
        parsed.error.find("openhi2txt extension") != std::string::npos,
        "legacy root rejects loop stop-when clearly");
    parsed = XmlParser::parseWithDiagnostics(openDefinition(stopBody));
    expect(parsed.ok && parsed.def.structures[0].items[0].loop.hasStopCondition,
        "open root accepts and parses loop stop-when");

    const std::string sourceBody =
        "<output><table><column id=\"NAME\" source-row=\"output_index\"/>"
        "<column id=\"SCORE\"/></table></output>";
    parsed = XmlParser::parseWithDiagnostics(legacyDefinition(sourceBody));
    expect(!parsed.ok && parsed.error.find("openhi2txt extension") != std::string::npos,
        "legacy root rejects column source-row clearly");
    parsed = XmlParser::parseWithDiagnostics(openDefinition(sourceBody));
    expect(parsed.ok && parsed.def.outputs.at("").tables[0].cols[0].sourceRow ==
        SourceRowKind::OutputIndex,
        "open root accepts and parses output_index source rows");

    const std::string hiddenHeaderBody =
        "<output><table><column id=\"RANK\" src=\"index\" header=\"false\"/>"
        "<column id=\"SCORE\"/></table></output>";
    parsed = XmlParser::parseWithDiagnostics(legacyDefinition(hiddenHeaderBody));
    expect(!parsed.ok && parsed.error.find("openhi2txt extension") != std::string::npos,
        "legacy root rejects column header visibility clearly");
    parsed = XmlParser::parseWithDiagnostics(openDefinition(hiddenHeaderBody));
    expect(parsed.ok && !parsed.def.outputs.at("").tables[0].cols[0].headerVisible,
        "open root accepts hidden column headers");
    parsed = XmlParser::parseWithDiagnostics(openDefinition(
        "<output><table><column id=\"SCORE\" header=\"sometimes\"/>"
        "</table></output>"));
    expect(!parsed.ok && parsed.error.find("expected true or false") != std::string::npos,
        "column header visibility rejects ambiguous values");

    parsed = XmlParser::parseWithDiagnostics(openDefinition(
        "<structure><loop stop-when=\"SCORE:0\"><elt id=\"SCORE\" type=\"int\" size=\"1\"/>"
        "</loop></structure>"));
    expect(!parsed.ok && parsed.error.find("positive count") != std::string::npos,
        "terminated loops require an explicit positive count");
    parsed = XmlParser::parseWithDiagnostics(openDefinition(
        "<structure><loop count=\"2\" stop-when=\"BROKEN\"><elt id=\"SCORE\" type=\"int\" size=\"1\"/>"
        "</loop></structure>"));
    expect(!parsed.ok && parsed.error.find("ID:value") != std::string::npos,
        "malformed stop conditions are rejected");
    parsed = XmlParser::parseWithDiagnostics(openDefinition(
        "<structure><loop count=\"2\" stop-when=\"OTHER:0\"><elt id=\"SCORE\" type=\"int\" size=\"1\"/>"
        "</loop></structure>"));
    expect(!parsed.ok && parsed.error.find("not declared") != std::string::npos,
        "stop conditions must name an element in their loop");
    parsed = XmlParser::parseWithDiagnostics(openDefinition(
        "<output><table><column id=\"NAME\" source-row=\"future_mode\"/></table></output>"));
    expect(!parsed.ok && parsed.error.find("expected output_index") != std::string::npos,
        "unsupported source-row modes are rejected");

    const std::string difStructure =
        "<structure file=\"dif\" offset=\"0x1120\" length=\"0x424\">"
        "<elt id=\"VALUE\" type=\"int\" size=\"1\"/>"
        "</structure>";
    parsed = XmlParser::parseWithDiagnostics(legacyDefinition(difStructure));
    expect(!parsed.ok && parsed.error.find("openhi2txt extension") != std::string::npos,
        "legacy roots reject DIF-backed structures clearly");
    parsed = XmlParser::parseWithDiagnostics(openDefinition(difStructure));
    expect(parsed.ok && parsed.def.structures.size() == 1 &&
        parsed.def.structures[0].hasInputWindow &&
        parsed.def.structures[0].inputOffset == 0x1120 &&
        parsed.def.structures[0].inputLength == 0x424,
        "open roots parse 64-bit DIF logical byte windows");

    parsed = XmlParser::parseWithDiagnostics(openDefinition(
        "<structure file=\"dif\" offset=\"0\"/>"));
    expect(!parsed.ok && parsed.error.find("requires both offset and length") != std::string::npos,
        "DIF-backed structures require a bounded window");
    parsed = XmlParser::parseWithDiagnostics(openDefinition(
        "<structure file=\"dif\" offset=\"0\" length=\"0\"/>"));
    expect(!parsed.ok && parsed.error.find("positive") != std::string::npos,
        "DIF-backed structures reject empty windows");
    parsed = XmlParser::parseWithDiagnostics(openDefinition(
        "<structure file=\"eeprom\" offset=\"0\" length=\"1\"/>"));
    expect(!parsed.ok && parsed.error.find("only supported with file='dif'") != std::string::npos,
        "logical disk windows cannot change existing file inputs");

    const std::string rankedPoints =
        "<output><table><ranked-points><qualifier src=\"NAME\" points=\"5\"/>"
        "</ranked-points><column id=\"NAME\"/><column id=\"POINTS\"/></table></output>";
    parsed = XmlParser::parseWithDiagnostics(legacyDefinition(rankedPoints));
    expect(!parsed.ok && parsed.error.find("openhi2txt extension") != std::string::npos,
        "legacy roots reject ranked-points tables clearly");
    parsed = XmlParser::parseWithDiagnostics(openDefinition(rankedPoints));
    expect(parsed.ok && parsed.def.outputs.at("").tables[0].rankedPoints.enabled &&
        parsed.def.outputs.at("").tables[0].rankedPoints.sources.size() == 1,
        "open roots parse ranked-points qualifier sources");
    parsed = XmlParser::parseWithDiagnostics(openDefinition(
        "<output><table><ranked-points/></table></output>"));
    expect(!parsed.ok && parsed.error.find("at least one qualifier") != std::string::npos,
        "ranked-points tables require qualifier sources");
    parsed = XmlParser::parseWithDiagnostics(openDefinition(
        "<output><table><ranked-points><qualifier src=\"NAME\" points=\"0\"/>"
        "</ranked-points></table></output>"));
    expect(!parsed.ok && parsed.error.find("positive integer") != std::string::npos,
        "ranked-points qualifiers require positive point ceilings");
}

void testMissingDifBehavior() {
    using namespace openhi2txt;

    const GameDef def = parseDefinition(openDefinition(
        "<structure file=\"dif\" offset=\"0\" length=\"1\">"
        "<elt id=\"VALUE\" type=\"int\" size=\"1\"/>"
        "</structure>"), "missing-DIF definition parses");
    const auto result = InputProcessor::process(
        std::filesystem::temp_directory_path() / "openhi2txt-no-such-mame-root",
        "missinggame", def);
    expect(!result.ok && result.errorKind == HiScoreErrorKind::InputNotFound,
        "a missing DIF behaves like a missing hi or NVRAM input");
}

void testTerminatedLoopBehavior() {
    using namespace openhi2txt;

    const std::string body =
        "<structure>"
        "<loop count=\"8\" stop-when=\"SCORE:0\">"
        "<elt id=\"SCORE\" type=\"int\" size=\"4\" base=\"16\"/>"
        "</loop>"
        "<elt id=\"AFTER\" type=\"int\" size=\"1\"/>"
        "</structure>";
    const GameDef def = parseDefinition(openDefinition(body), "terminated-loop definition parses");
    const std::vector<uint8_t> raw{
        0x00, 0x01, 0x96, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x99, 0x99, 0x99, 0x99
    };
    const auto rows = Processor::extractRows(raw, def.structures[0], def);
    expect(rows.size() == 1 && valueAt(rows, 0, "SCORE") == 19600,
        "a score ending in zero bytes is committed and only the complete zero record terminates");
    expect(valueAt(rows, 0, "AFTER") == 0x99,
        "the terminating record is consumed before extraction continues");

    const std::string stagedBody =
        "<structure><loop count=\"3\" stop-when=\"SCORE:0\">"
        "<elt id=\"TAG\" type=\"int\" size=\"1\"/>"
        "<elt id=\"SCORE\" type=\"int\" size=\"1\"/>"
        "</loop><elt id=\"AFTER\" type=\"int\" size=\"1\"/></structure>";
    const GameDef staged = parseDefinition(openDefinition(stagedBody), "staged-iteration definition parses");
    const auto stagedRows = Processor::extractRows({ 7, 5, 8, 0, 9 }, staged.structures[0], staged);
    expect(stagedRows.size() == 1 && valueAt(stagedRows, 0, "TAG") == 7 &&
        valueAt(stagedRows, 0, "SCORE") == 5,
        "all values from the terminating iteration are discarded together");
    expect(valueAt(stagedRows, 0, "AFTER") == 9,
        "all bytes in a multi-element terminating iteration are consumed");

    const std::string cappedBody =
        "<structure>"
        "<loop count=\"2\" stop-when=\"SCORE:0\">"
        "<elt id=\"SCORE\" type=\"int\" size=\"1\"/>"
        "</loop><elt id=\"AFTER\" type=\"int\" size=\"1\"/>"
        "</structure>";
    const GameDef capped = parseDefinition(openDefinition(cappedBody), "count-ceiling definition parses");
    const auto cappedRows = Processor::extractRows({ 4, 5, 6 }, capped.structures[0], capped);
    expect(cappedRows.size() == 2 && valueAt(cappedRows, 0, "SCORE") == 4 &&
        valueAt(cappedRows, 1, "SCORE") == 5,
        "count remains the maximum when no terminator is found");
    expect(valueAt(cappedRows, 0, "AFTER") == 6,
        "the count ceiling leaves following data for subsequent structure elements");
}

void testPositionalSourceRowBehavior() {
    using namespace openhi2txt;

    const std::string body =
        "<output><table sort=\"SCORE\" sort-order=\"desc\" lines-max=\"2\">"
        "<column id=\"RANK\" src=\"index\" format=\"+1\"/>"
        "<column id=\"LABEL\" source-row=\"output_index\"/>"
        "<column id=\"SCORE\"/>"
        "</table></output>";

    const GameDef def = parseDefinition(openDefinition(body), "positional-source definition parses");
    std::vector<std::unordered_map<std::string, Value>> rows(3);
    rows[0]["LABEL"] = std::string("ALPHA");
    rows[0]["SCORE"] = int64_t{ 10 };
    rows[1]["LABEL"] = std::string("BETA");
    rows[1]["SCORE"] = int64_t{ 30 };
    rows[2]["SCORE"] = int64_t{ 20 };

    const auto result = ResultRenderer::render(def, rows, "", ReadOptions{});
    expect(result.tables.size() == 1 && result.tables[0].rows.size() == 2,
        "score-only candidate rows remain relevant and participate in sorting");
    expect(result.tables[0].rows[0] == std::vector<std::string>({ "1", "ALPHA", "30" }),
        "the first output row reads its label from original logical row zero");
    expect(result.tables[0].rows[1] == std::vector<std::string>({ "2", "BETA", "20" }),
        "the second output row reads its label from original logical row one");
}

void testRankedPointsBehavior() {
    using namespace openhi2txt;

    const std::string body =
        "<output><table id=\"LEGENDS\" sort=\"POINTS\" sort-order=\"desc\" lines-max=\"5\">"
        "<ranked-points name-column=\"NAME\" points-column=\"POINTS\">"
        "<qualifier src=\"SPEED NAME\" points=\"3\"/>"
        "<qualifier src=\"FATALS NAME\" points=\"3\"/>"
        "<qualifier src=\"STREAK NAME\" points=\"3\"/>"
        "<qualifier src=\"HITS NAME\" points=\"2\"/>"
        "<qualifier src=\"DAMAGE NAME\" points=\"2\"/>"
        "</ranked-points>"
        "<column id=\"RANK\" src=\"index\" format=\"+1\"/>"
        "<column id=\"NAME\"/><column id=\"POINTS\"/>"
        "</table></output>";
    const GameDef def = parseDefinition(openDefinition(body), "ranked-points definition parses");

    std::vector<std::unordered_map<std::string, Value>> rows(3);
    rows[0]["SPEED NAME"] = std::string("MB ");
    rows[0]["FATALS NAME"] = std::string("MB");
    rows[0]["STREAK NAME"] = std::string("ROB");
    rows[0]["HITS NAME"] = std::string("KEV");
    rows[0]["DAMAGE NAME"] = std::string("KEV");
    rows[1]["SPEED NAME"] = std::string("MB");
    rows[1]["FATALS NAME"] = std::string("ZED");
    rows[1]["STREAK NAME"] = std::string("ROB");
    rows[1]["HITS NAME"] = std::string("KEV");
    rows[1]["DAMAGE NAME"] = std::string("SAM");
    rows[2]["SPEED NAME"] = std::string("ACE");
    rows[2]["FATALS NAME"] = std::string("MB");
    rows[2]["STREAK NAME"] = std::string("CEM");

    const auto result = ResultRenderer::render(def, rows, "", ReadOptions{});
    expect(result.tables.size() == 1 && result.tables[0].rows.size() == 5,
        "ranked-points output is sorted and limited like an ordinary table");
    expect(result.tables[0].rows[0] == std::vector<std::string>({ "1", "MB", "6" }),
        "a name earns points from independent qualifier tables");
    expect(result.tables[0].rows[1] == std::vector<std::string>({ "2", "KEV", "4" }),
        "independent character qualifiers accumulate");
    expect(result.tables[0].rows[2] == std::vector<std::string>({ "3", "ROB", "3" }),
        "repeat appearances in one qualifier count only at the best rank");
    expect(result.tables[0].rows[3] == std::vector<std::string>({ "4", "ZED", "2" }),
        "lower qualifier ranks receive one fewer point");
    expect(result.tables[0].rows[4] == std::vector<std::string>({ "5", "ACE", "1" }),
        "equal totals retain first-seen qualifier order");
}

void testColumnHeaderVisibility() {
    using namespace openhi2txt;

    const GameDef partial = parseDefinition(openDefinition(
        "<output><table id=\"SCORES\">"
        "<column id=\"RANK\" src=\"index\" format=\"+1\" header=\"false\"/>"
        "<column id=\"NAME\"/><column id=\"SCORE\"/>"
        "</table></output>"), "partially hidden header definition parses");
    std::vector<std::unordered_map<std::string, Value>> rows(1);
    rows[0]["NAME"] = std::string("XYZ");
    rows[0]["SCORE"] = int64_t{ 20202 };

    auto result = ResultRenderer::render(partial, rows, "", ReadOptions{});
    expect(result.tables.size() == 1 &&
        result.tables[0].columns == std::vector<std::string>({ "", "NAME", "SCORE" }),
        "a hidden heading retains its positional column slot");
    expect(printed(result, OutputFormat::Text) ==
        "# SCORES\n|NAME|SCORE\n1|XYZ|20202\n\n",
        "text output blanks an individually hidden heading but keeps its data");
    expect(printed(result, OutputFormat::Xml).find(
        "<col></col><col>NAME</col><col>SCORE</col>") != std::string::npos,
        "XML output preserves positional placeholders for individually hidden headings");

    const GameDef allHidden = parseDefinition(openDefinition(
        "<output><table id=\"SCORES\">"
        "<column id=\"RANK\" src=\"index\" format=\"+1\" header=\"false\"/>"
        "<column id=\"NAME\" header=\"false\"/>"
        "<column id=\"SCORE\" header=\"false\"/>"
        "</table></output>"), "fully hidden header definition parses");
    result = ResultRenderer::render(allHidden, rows, "", ReadOptions{});
    expect(printed(result, OutputFormat::Text) ==
        "# SCORES\n1|XYZ|20202\n\n",
        "text output omits the header line when every heading is hidden");
    expect(printed(result, OutputFormat::Xml).find("<col>") == std::string::npos,
        "XML output omits the header block when every heading is hidden");
}

void testMidwaySortBehavior() {
    using namespace openhi2txt;

    const std::string body =
        "<output sort-method=\"midway\">"
        "<table display=\"sort\" sort=\"SEED\" sort-order=\"desc\" "
        "line-ignore=\"ELIGIBLE:1\" line-ignore-operator=\"&lt;\">"
        "<column id=\"NAME\"/></table>"
        "<table id=\"RESULT\" sort=\"SCORE\" sort-order=\"desc\" "
        "line-ignore=\"ELIGIBLE:1\" line-ignore-operator=\"&lt;\">"
        "<sort src=\"TIE\" order=\"desc\"/>"
        "<column id=\"NAME\"/><column id=\"SCORE\"/>"
        "</table></output>";

    auto parsed = XmlParser::parseWithDiagnostics(legacyDefinition(body));
    expect(!parsed.ok && parsed.error.find("openhi2txt root") != std::string::npos,
        "legacy roots reject Midway sorting extensions clearly");
    const GameDef def = parseDefinition(openDefinition(body),
        "Midway sorting definition parses");
    expect(Utils::ieq(def.outputs.at("").sortMethod, "midway") &&
        def.outputs.at("").tables[1].sortKeys.size() == 2,
        "Midway output mode and secondary sort keys are retained");
    parsed = XmlParser::parseWithDiagnostics(openDefinition(
        "<output sort-method=\"stable-ish\"><table><column id=\"X\"/>"
        "</table></output>"));
    expect(!parsed.ok && parsed.error.find("expected midway") != std::string::npos,
        "unknown output sort methods are rejected clearly");
    parsed = XmlParser::parseWithDiagnostics(openDefinition(
        "<output><table><sort order=\"desc\"/><column id=\"X\"/>"
        "</table></output>"));
    expect(!parsed.ok && parsed.error.find("non-empty src") != std::string::npos,
        "secondary sort keys require an explicit source");

    const int seeds[]  = { 2, 1, 2, 1, 2, 1, 2, 1, 2, 1 };
    const int scores[] = { 1, 1, 2, 2, 1, 1, 2, 2, 1, 1 };
    std::vector<std::unordered_map<std::string, Value>> rows(10);
    for (size_t i = 0; i < rows.size(); ++i) {
        rows[i]["NAME"] = std::string(1, static_cast<char>('A' + i));
        rows[i]["SEED"] = static_cast<int64_t>(seeds[i]);
        rows[i]["SCORE"] = static_cast<int64_t>(scores[i]);
        rows[i]["TIE"] = int64_t{ 0 };
        rows[i]["ELIGIBLE"] = int64_t{ 1 };
    }
    for (size_t i = 0; i < 32; ++i) {
        std::unordered_map<std::string, Value> empty;
        empty["NAME"] = std::string("EMPTY") + std::to_string(i);
        empty["SEED"] = empty["SCORE"] = empty["TIE"] = empty["ELIGIBLE"] = int64_t{ 0 };
        rows.push_back(std::move(empty));
    }

    const auto result = ResultRenderer::render(def, rows, "", ReadOptions{});
    expect(result.tables.size() == 1 && result.tables[0].id == "RESULT",
        "a display=sort table changes state without being emitted");
    const std::vector<std::string> expected{ "D", "C", "H", "G", "E", "I", "B", "A", "F", "J" };
    bool exactOrder = result.tables[0].rows.size() == expected.size();
    for (size_t i = 0; exactOrder && i < expected.size(); ++i)
        exactOrder = result.tables[0].rows[i][0] == expected[i];
    expect(exactOrder,
        "Midway sorting reproduces DJGPP/BSD tie permutations and chained row order");
    expect(result.tables[0].rows.size() == 10,
        "Midway eligibility rules participate in sorting and suppress ineligible profiles");

    const GameDef bitCount = parseDefinition(openDefinition(
        "<output><table><column id=\"MASK\" format=\"BITS\"/></table></output>"
        "<format id=\"BITS\"><bit-count/></format>"),
        "bit-count definition parses");
    std::vector<std::unordered_map<std::string, Value>> masks(1);
    masks[0]["MASK"] = int64_t{ 0xB5 };
    const auto bitResult = ResultRenderer::render(bitCount, masks, "", ReadOptions{});
    expect(bitResult.tables[0].rows[0][0] == "5",
        "bit-count counts set bits for Midway team masks");

    const GameDef emptyTable = parseDefinition(openDefinition(
        "<output><table id=\"CHAMPIONS\" show-empty=\"true\" "
        "line-ignore=\"MASK:30\" line-ignore-operator=\"&lt;\">"
        "<column id=\"NAME\"/><column id=\"MASK\"/>"
        "</table></output>"), "show-empty definition parses");
    std::vector<std::unordered_map<std::string, Value>> nonChampions(1);
    nonChampions[0]["NAME"] = std::string("ALMOST");
    nonChampions[0]["MASK"] = int64_t{ 29 };
    const auto emptyResult = ResultRenderer::render(emptyTable, nonChampions, "", ReadOptions{});
    expect(emptyResult.tables.size() == 1 && emptyResult.tables[0].id == "CHAMPIONS" &&
        emptyResult.tables[0].rows.empty(),
        "show-empty emits a table whose filters remove every row");
    parsed = XmlParser::parseWithDiagnostics(legacyDefinition(
        "<output><table show-empty=\"true\"><column id=\"X\"/></table></output>"));
    expect(!parsed.ok && parsed.error.find("openhi2txt extension") != std::string::npos,
        "legacy roots reject show-empty clearly");
}

} // namespace

int main() {
    testRootParityAndOrdinaryRegressions();
    testRootAwareValidation();
    testMissingDifBehavior();
    testTerminatedLoopBehavior();
    testPositionalSourceRowBehavior();
    testRankedPointsBehavior();
    testColumnHeaderVisibility();
    testMidwaySortBehavior();

    if (failures != 0) {
        std::cerr << failures << " test(s) failed.\n";
        return 1;
    }

    std::cout << "All OpenHi2txt extension tests passed.\n";
    return 0;
}
