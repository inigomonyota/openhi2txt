#include "app/InputProcessor.h"
#include "core/InputPlanner.h"
#include "core/Processor.h"
#include "core/ResultRenderer.h"
#include "io/Utils.h"
#include "xml/XmlParser.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <sstream>
#include <string>
#include <vector>

using namespace openhi2txt;

namespace {

[[noreturn]] void fail(const std::string& message) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
}

void require(bool condition, const std::string& message) {
    if (!condition) fail(message);
}

bool hasRange(const HiScoreInputPlan& plan, uint64_t source, uint64_t length) {
    for (const auto& range : plan.watchRanges) {
        if (range.offset == source && range.length == length) return true;
    }
    return false;
}

Elt elt(std::string id, int size, int offset = -1) {
    Elt result;
    result.id = std::move(id);
    result.type = "raw";
    result.size = size;
    result.offset = offset;
    return result;
}

StructureItem item(Elt value) {
    StructureItem result;
    result.kind = StructureItem::Kind::Elt;
    result.elt = std::move(value);
    return result;
}

GameDef dependencyDefinition() {
    GameDef def;
    FormatDef calculated;
    calculated.id = "CALCULATED";
    MathOp multiply;
    multiply.kind = FormatKind::Multiply;
    multiply.hasReference = true;
    multiply.reference.id = "MULTIPLIER";
    calculated.mathOps.push_back(multiply);
    def.formats.emplace(calculated.id, calculated);

    Structure structure;
    structure.fileKind = "nvram";
    structure.checkSizes.push_back(256);
    structure.checkAll.push_back(CheckDef{0, {0x55}});
    structure.items.push_back(item(elt("UNKNOWN", 100)));
    Elt multiplier = elt("MULTIPLIER", 1);
    multiplier.type = "int";
    structure.items.push_back(item(multiplier));
    Elt score = elt("SCORE", 4);
    score.type = "int";
    score.format = "CALCULATED";
    structure.items.push_back(item(score));
    structure.items.push_back(item(elt("UNUSED", 15)));
    Elt name = elt("NAME", 3);
    name.type = "text";
    name.tableIndexKind = TableIndexKind::IndexFromValue;
    name.tableIndexCol = "SLOT";
    structure.items.push_back(item(name));
    Elt slot = elt("SLOT", 1);
    slot.type = "int";
    structure.items.push_back(item(slot));
    Elt rank = elt("RANK", 1);
    rank.type = "int";
    structure.items.push_back(item(rank));
    def.structures.push_back(structure);

    OutputDef output;
    Table table;
    Column scoreColumn;
    scoreColumn.id = "POINTS";
    scoreColumn.src = "SCORE";
    table.cols.push_back(scoreColumn);
    Column nameColumn;
    nameColumn.id = "PLAYER";
    nameColumn.src = "NAME";
    table.cols.push_back(nameColumn);
    table.sortKeys.push_back(SortKeyDef{"RANK", "ascending", ""});
    output.tables.push_back(table);
    def.outputs.emplace("", output);
    def.outputOrder.push_back("");
    return def;
}

void testHiddenValidatedElementIsWatched() {
    GameDef def;
    Structure structure;
    structure.items.push_back(item(elt("UNKNOWN", 20)));
    Elt hiddenBcd = elt("HIDDEN BCD", 2);
    hiddenBcd.type = "int";
    hiddenBcd.intBase = IntBaseKind::BcdBE;
    structure.items.push_back(item(hiddenBcd));
    structure.items.push_back(item(elt("SCORE", 1)));
    def.structures.push_back(structure);
    OutputDef output;
    Column score;
    score.id = "SCORE";
    output.fields.push_back(score);
    def.outputs.emplace("", output);

    const auto plan = InputPlanner::plan(def).front();
    require(hasRange(plan, 20, 3),
        "hidden validated BCD and adjacent output bytes are watched together");
}

void testDependenciesAndCoalescing() {
    const auto plans = InputPlanner::plan(dependencyDefinition());
    require(plans.size() == 1, "expected one structure plan");
    const auto& plan = plans.front();
    require(plan.fileKind == "nvram", "file kind is preserved");
    require(plan.acceptedBufferSizes == std::vector<uint64_t>{256},
        "declared size is exposed");
    require(hasRange(plan, 0, 1), "structure check byte is included");
    require(hasRange(plan, 100, 5),
        "score and its formatted operand are included and coalesced");
    require(hasRange(plan, 120, 5),
        "name, table-index source, and sort source are included and coalesced");
    require(plan.watchRanges.size() == 3, "unused leading and trailing bytes are excluded");
}

void testUnwatchedBytesCannotChangeRenderedResult() {
    const GameDef def = dependencyDefinition();
    const auto plan = InputPlanner::plan(def).front();
    std::vector<uint8_t> bytes(256, 0);
    bytes[0] = 0x55;
    bytes[100] = 3;
    bytes[104] = 2;
    bytes[120] = 'A';
    bytes[121] = 'B';
    bytes[122] = 'C';
    bytes[123] = 0;
    bytes[124] = 1;

    auto renderRows = [&](const std::vector<uint8_t>& input) {
        const auto rows = Processor::extractRows(input, def.structures.front(), def);
        return ResultRenderer::render(def, rows, "", ReadOptions{}).tables.front().rows;
    };
    const auto baseline = renderRows(bytes);

    auto watched = [&](size_t offset) {
        for (const auto& range : plan.watchRanges)
            if (offset >= range.offset && offset - range.offset < range.length) return true;
        return false;
    };
    for (size_t offset = 0; offset < bytes.size(); ++offset) {
        if (watched(offset)) continue;
        auto changed = bytes;
        changed[offset] ^= 0xff;
        require(renderRows(changed) == baseline,
            "changing unwatched byte " + std::to_string(offset) + " changed rendered output");
    }

    auto changedScore = bytes;
    changedScore[104] = 4;
    require(renderRows(changedScore) != baseline,
        "the fixture must prove that a watched score byte changes output");
}

void testStoppedLoopConservativelyIncludesTail() {
    GameDef def;
    Structure structure;
    structure.items.push_back(item(elt("PREFIX", 10)));
    StructureItem loopItem;
    loopItem.kind = StructureItem::Kind::Loop;
    loopItem.loop.count = 3;
    loopItem.loop.hasStopCondition = true;
    loopItem.loop.stopFieldId = "END";
    loopItem.loop.stopValue = "255";
    loopItem.loop.elts.push_back(elt("SCORE", 1));
    loopItem.loop.elts.push_back(elt("END", 1));
    structure.items.push_back(loopItem);
    structure.items.push_back(item(elt("FOOTER", 1)));
    def.structures.push_back(structure);

    OutputDef output;
    Table table;
    Column column;
    column.id = "SCORE";
    table.cols.push_back(column);
    output.tables.push_back(table);
    def.outputs.emplace("", output);

    const auto plan = InputPlanner::plan(def).front();
    require(plan.watchRanges.size() == 1 && hasRange(plan, 10, 7),
        "a stop condition includes every possible loop and shifted-tail byte");
}

void testSourceWindowAndByteSwap() {
    GameDef def;
    Structure structure;
    structure.fileKind = "dif";
    structure.hasInputWindow = true;
    structure.inputOffset = 0x1000;
    structure.inputLength = 64;
    structure.byteSwap = 4;
    structure.items.push_back(item(elt("UNKNOWN", 9)));
    structure.items.push_back(item(elt("SCORE", 2)));
    def.structures.push_back(structure);

    OutputDef output;
    Column field;
    field.id = "SCORE";
    output.fields.push_back(field);
    def.outputs.emplace("", output);

    const auto plan = InputPlanner::plan(def).front();
    require(plan.sourceWindowOffset == 0x1000 && plan.sourceWindowLength == 64,
        "DIF logical source window is exposed");
    require(plan.watchRanges.size() == 1 && hasRange(plan, 0x1008, 4),
        "structure byte swap expands to the complete source chunk");
}

void testDecoderRegionIncludesCipherAndChecksum() {
    GameDef def;
    Structure structure;
    structure.decodeRegions.push_back(DecodeRegion{"namco-system12", 20, 16});
    def.structures.push_back(structure);
    OutputDef output;
    def.outputs.emplace("", output);

    const auto plan = InputPlanner::plan(def).front();
    require(hasRange(plan, 20, 18),
        "decoder region includes ciphertext and checksum seed bytes");
}

} // namespace

int auditDefinitions(const std::string& definitionsDirectory) {
    size_t definitions = 0;
    size_t structures = 0;
    size_t emptyPlans = 0;
    size_t invalidRanges = 0;
    std::map<std::string, size_t> kinds;
    for (const auto& entry : std::filesystem::directory_iterator(definitionsDirectory)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".xml" ||
            entry.path().stem().string().front() == '_') continue;
        std::ifstream input(entry.path(), std::ios::binary);
        std::ostringstream contents;
        contents << input.rdbuf();
        auto parsed = XmlParser::parseWithDiagnostics(contents.str());
        if (!parsed.ok) {
            std::cerr << "AUDIT: " << entry.path().filename().string() << ": "
                      << parsed.error << '\n';
            return 1;
        }
        if (parsed.def.structures.empty()) continue; // <sameas> alias
        const auto result = InputPlanner::plan(parsed.def);
        ++definitions;
        for (const auto& plan : result) {
            ++structures;
            ++kinds[plan.fileKind];
            if (plan.watchRanges.empty()) {
                ++emptyPlans;
                std::cerr << "AUDIT empty: " << entry.path().stem().string() << " structure "
                          << plan.structureIndex << '\n';
            }
            for (const auto& range : plan.watchRanges) {
                if (range.length == 0 || range.offset > UINT64_MAX - range.length) {
                    ++invalidRanges;
                    std::cerr << "AUDIT invalid range: " << entry.path().stem().string() << " structure "
                              << plan.structureIndex << '\n';
                }
                if (plan.sourceWindowLength != 0 &&
                    (range.offset < plan.sourceWindowOffset ||
                     range.offset + range.length - plan.sourceWindowOffset >
                        plan.sourceWindowLength)) {
                    ++invalidRanges;
                    std::cerr << "AUDIT range outside window: " << entry.path().stem().string() << " structure "
                              << plan.structureIndex << '\n';
                }
            }
        }
    }

    std::cout << "Audited " << definitions << " definitions and " << structures
              << " structures; empty=" << emptyPlans << ", invalid=" << invalidRanges << '\n';
    for (const auto& kind : kinds)
        std::cout << "  " << kind.first << ": " << kind.second << '\n';
    return invalidRanges == 0 ? 0 : 1;
}

std::string resultFingerprint(const HiScoreResult& result) {
    std::ostringstream out;
    for (const auto& table : result.tables) {
        out << "T:" << table.id << '\n';
        for (const auto& row : table.rows) {
            for (const auto& value : row) out << value.size() << ':' << value;
            out << '\n';
        }
    }
    for (const auto& field : result.fields)
        out << "F:" << field.id << ':' << field.value.size() << ':' << field.value << '\n';
    return out.str();
}

int simulateFixture(const std::string& definitionPath,
                    const std::string& inputPath,
                    const std::string& fileKind) {
    std::ifstream xmlInput(definitionPath, std::ios::binary);
    std::ostringstream xml;
    xml << xmlInput.rdbuf();
    auto parsed = XmlParser::parseWithDiagnostics(xml.str());
    if (!parsed.ok) fail("fixture definition did not parse: " + parsed.error);

    std::ifstream byteInput(inputPath, std::ios::binary);
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(byteInput)),
                               std::istreambuf_iterator<char>());
    if (bytes.empty()) fail("fixture input is empty or unreadable");

    const auto plans = InputPlanner::plan(parsed.def);
    std::vector<HiScoreWatchRange> watchedRanges;
    for (const auto& plan : plans) {
        if (!Utils::ieq(plan.fileKind, fileKind)) continue;
        if (!plan.acceptedBufferSizes.empty() &&
            std::find(plan.acceptedBufferSizes.begin(), plan.acceptedBufferSizes.end(), bytes.size()) ==
                plan.acceptedBufferSizes.end()) continue;
        watchedRanges.insert(watchedRanges.end(), plan.watchRanges.begin(), plan.watchRanges.end());
    }
    if (watchedRanges.empty()) fail("no applicable watch plan for fixture");

    ReadOptions options;
    options.includeExtra = true;
    options.includeDebug = true;
    std::string lastDecodeError;
    auto render = [&](const std::vector<uint8_t>& source, std::string& fingerprint) {
        auto processed = InputProcessor::processBuffers(
            parsed.def, {HiScoreInput{fileKind, source, "fixture"}});
        if (!processed.ok) {
            lastDecodeError = processed.error;
            return false;
        }
        lastDecodeError.clear();
        const auto result = ResultRenderer::render(
            parsed.def, processed.rows, processed.outputId, options);
        fingerprint = resultFingerprint(result);
        return result.ok;
    };

    std::string baseline;
    if (!render(bytes, baseline) || baseline.empty()) fail("fixture did not decode to output");
    auto watched = [&](size_t offset) {
        for (const auto& range : watchedRanges) {
            if (offset >= range.offset && offset - range.offset < range.length) return true;
        }
        return false;
    };

    size_t unwatchedChecked = 0;
    const size_t stride = std::max<size_t>(1, bytes.size() / 32);
    for (size_t offset = 0; offset < bytes.size() && unwatchedChecked < 32; offset += stride) {
        if (watched(offset)) continue;
        auto changed = bytes;
        changed[offset] ^= 0xff;
        std::string output;
        if (!render(changed, output) || output != baseline) {
            std::ostringstream detail;
            detail << "unwatched fixture byte changed decoded output at " << offset
                   << "; planned ranges:";
            for (const auto& range : watchedRanges)
                detail << ' ' << range.offset << '+' << range.length;
            const size_t difference = std::mismatch(
                baseline.begin(), baseline.end(), output.begin(), output.end()).first - baseline.begin();
            detail << "; first output difference=" << difference
                   << "; before=" << baseline.substr(difference, 80)
                   << "; after=" << output.substr(difference, 80)
                   << "; decode-error=" << lastDecodeError;
            fail(detail.str());
        }
        ++unwatchedChecked;
    }

    bool observedOutputChange = false;
    size_t changedOffset = 0;
    size_t attempts = 0;
    for (const auto& range : watchedRanges) {
        const uint64_t end = std::min<uint64_t>(bytes.size(), range.offset + range.length);
        for (uint64_t offset = range.offset; offset < end && attempts < 4096; ++offset, ++attempts) {
            auto changed = bytes;
            changed[static_cast<size_t>(offset)] ^= 1;
            std::string output;
            if (render(changed, output) && output != baseline) {
                observedOutputChange = true;
                changedOffset = static_cast<size_t>(offset);
                break;
            }
        }
        if (observedOutputChange || attempts >= 4096) break;
    }
    require(observedOutputChange, "no watched fixture mutation changed decoded output");

    uint64_t watchedBytes = 0;
    for (const auto& range : watchedRanges) watchedBytes += range.length;
    std::cout << "Fixture passed: kind=" << fileKind << ", source=" << bytes.size()
              << " bytes, watched=" << watchedBytes << " bytes in "
              << watchedRanges.size() << " ranges, sampled-unwatched=" << unwatchedChecked
              << ", output-changing-byte=" << changedOffset << '\n';
    return 0;
}

int main(int argc, char** argv) {
    if (argc == 5 && std::string(argv[1]) == "--fixture")
        return simulateFixture(argv[2], argv[3], argv[4]);
    if (argc == 2) return auditDefinitions(argv[1]);
    testDependenciesAndCoalescing();
    testHiddenValidatedElementIsWatched();
    testUnwatchedBytesCannotChangeRenderedResult();
    testStoppedLoopConservativelyIncludesTail();
    testSourceWindowAndByteSwap();
    testDecoderRegionIncludesCipherAndChecksum();
    std::cout << "Input planner tests passed\n";
    return 0;
}
