#include "app/OutputPrinter.h"
#include "core/Processor.h"
#include "core/ResultRenderer.h"
#include "io/Utils.h"
#include "openhi2txt/openhi2txt.h"
#include "xml/XmlParser.h"

#include <cstdint>
#include <cstdio>
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

} // namespace

int main() {
    testRootParityAndOrdinaryRegressions();
    testRootAwareValidation();
    testTerminatedLoopBehavior();
    testPositionalSourceRowBehavior();

    if (failures != 0) {
        std::cerr << failures << " test(s) failed.\n";
        return 1;
    }

    std::cout << "All OpenHi2txt extension tests passed.\n";
    return 0;
}
