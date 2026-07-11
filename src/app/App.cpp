#include "app/App.h"
#include "app/Args.h"
#include "app/DefResolver.h"
#include "app/InputProcessor.h"
#include "app/OutputPrinter.h"
#include "core/ResultRenderer.h"
#include "core/Trace.h"
#include "io/ArchiveManager.h"
#include "xml/XmlParser.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace openhi2txt {

namespace {

void printUsage() {
    std::printf(
        "openhi2txt %s\n"
        "openhi2txt [-r|-ra|-rd|-v|-h] [-hiscoredat <hiscoredat_path>] [-descr <xmls_path>] [-xml] [-keep-field \"<column>\"] [-keep-table-value \"<value>\"] [-keep-first-score <yes|no>] [-hide-field \"<column>\"] [-keep-first-table <yes|no>] [-max-lines <integer>] [-max-columns <integer>] [-score-grouping <yes|no>] [-score-grouping-separator \"<string>\"] [-score-grouping-size <integer>] [-notrace] <path>\n"
        "  -r: read main high score entries\n"
        "  -ra: read all high score entries\n"
        "  -rd: read all high score entries as well as debug entries; also activate traces\n"
        "  -hiscoredat or -hs: optional path to hiscore.dat related to the hiscore file to read. default is <path>/../../hiscore.dat\n"
        "  -descr or -ds: optional path to xml description db. default is ./db/\n"
        "  -xml: optional switch to get output in XML instead of TXT\n"
        "  -keep-field \"<field>\": column to filter lines to keep only the ones having a specific column/value couple\n"
        "  -keep-table-value \"<value>\": field value to filter lines to keep only the ones having a specific column/value couple (repeat this argument as much as necessary)\n"
        "  -keep-first-score <yes|no>: in case of lines filtering, always keep the first one\n"
        "  -keep-first-table <yes|no>: display only the 1st table\n"
        "  -hide-field \"<field>\": hide the column (repeat this argument as much as necessary)\n"
        "  -hide-column \"<field>\": same as -hide-field\n"
        "  -score-grouping <yes|no>: split each SCORE by group of integers\n"
        "  -score-grouping-separator \"<string>\": separator to split SCORE (default is '.')\n"
        "  -score-grouping-size <integer>: size of integers group when splitting SCORE (default is 3)\n"
        "  -max-lines <integer>: display a maximum of <integer> lines for each table displayed\n"
        "  -max-columns <integer>: display a maximum of <integer> columns for each table displayed\n"
        "  -l: list games\n"
        "  -location: display supported memory location when listing games\n"
        "  -notrace: disable trace, even in debug display\n"
        "  -trace: enable trace\n"
        "  -v: display version\n"
        "  -h: display help\n"
        "  <path>: game hi file to read\n"
        "\n"
        "OpenHi2txt also accepts: --defs <xmls_path> --mame-root <mame_root> --game <romname>\n",
        VersionString);
}

int printGameList(const std::filesystem::path& defsZip, bool showLocations) {
    std::vector<std::string> games = ArchiveManager::listBasenames(defsZip, ".xml");
    for (auto& g : games) {
        if (g.size() >= 4 && g.substr(g.size() - 4) == ".xml") {
            g.resize(g.size() - 4);
        }
    }
    games.erase(std::remove(games.begin(), games.end(), "hi2txt"), games.end());

    for (const auto& game : games) {
        if (!showLocations) {
            std::printf("%s\n", game.c_str());
            continue;
        }

        std::string xml;
        std::vector<std::string> locations;
        if (ArchiveManager::extractBest(defsZip, game + ".xml", xml)) {
            XmlParseResult parsed = XmlParser::parseWithDiagnostics(xml);
            if (parsed.ok) {
                for (const auto& s : parsed.def.structures) {
                    std::string kind = s.fileKind.empty() ? ".hi" : s.fileKind;
                    if (std::find(locations.begin(), locations.end(), kind) == locations.end()) {
                        locations.push_back(std::move(kind));
                    }
                }
            }
        }

        std::printf("%s", game.c_str());
        for (const auto& loc : locations) {
            std::printf(" %s", loc.c_str());
        }
        std::printf("\n");
    }
    return 0;
}

} // namespace

int App::run(int argc, char** argv) {
    Args args = Args::parse(argc, argv);
    if (!args.ok) { std::fprintf(stderr, "%s\n", args.error.c_str()); return 2; }

    if (args.action == CliAction::Help) {
        printUsage();
        return 0;
    }
    if (args.action == CliAction::Version) {
        std::printf("%s\n", VersionString);
        return 0;
    }
    if (args.action == CliAction::List) {
        return printGameList(args.defsZip, args.showLocations);
    }

    TraceSink trace;
    if (args.trace) {
        trace.write = [](const std::string& line) {
            std::printf("%s\n", line.c_str());
        };

        const std::filesystem::path hiscoreDat = args.hiscoreDat.empty()
            ? (args.mameRoot / "plugins" / "hiscore" / "hiscore.dat")
            : args.hiscoreDat;
        const std::filesystem::path gameHi = args.inputPath.empty()
            ? (args.mameRoot / "hiscore" / (args.game + ".hi"))
            : args.inputPath;
        trace.line("TRACE: working directory: " + std::filesystem::current_path().string());
        trace.line("TRACE: hiscore.dat: " + hiscoreDat.string());
        trace.line("TRACE: game high score: " + gameHi.string());
        trace.line("TRACE: game detected: " + args.game);
    }
    const TraceSink* tracePtr = args.trace ? &trace : nullptr;

    ReadOptions options;
    options.includeExtra = args.showExtra;
    options.includeDebug = args.showDebug;
    options.keepFields = args.keepFields;
    options.hideFields = args.hideFields;
    options.keepTableValues = args.keepTableValues;
    options.keepFirstScore = args.keepFirstScore;
    options.keepFirstTable = args.keepFirstTable;
    options.maxLines = args.maxLines;
    options.maxColumns = args.maxColumns;
    options.scoreGrouping = args.scoreGrouping;
    options.scoreGroupingSeparator = args.scoreGroupingSeparator;
    options.scoreGroupingSize = args.scoreGroupingSize;

    auto defRes = DefResolver::loadFromZip(args.defsZip, args.mameRoot, args.game, args.hiscoreDat, tracePtr);
    if (!defRes.ok) {
        std::fprintf(stderr, "%s\n", defRes.error.c_str());
        return 2;
    }

    auto inRes = InputProcessor::process(args.mameRoot, args.game, defRes.def, args.inputPath, tracePtr);
    if (!inRes.ok) {
        if (inRes.error.rfind("No matching structure found", 0) == 0) {
            if (args.xmlOutput) {
                std::printf("<hi2txt>\n</hi2txt>\n");
            }
            return 0;
        }
        std::fprintf(stderr, "ERROR: %s\n", inRes.error.c_str());
        return 2;
    }

    HiScoreResult result = ResultRenderer::render(defRes.def, inRes.rows, inRes.outputId, options, tracePtr);
    if (!result.ok) {
        if (result.error.rfind("No matching structure found", 0) == 0) {
            if (args.xmlOutput) {
                std::printf("<hi2txt>\n</hi2txt>\n");
            }
            return 0;
        }
        if (result.error.rfind("ERROR:", 0) == 0) {
            std::fprintf(stderr, "%s\n", result.error.c_str());
        }
        else {
            std::fprintf(stderr, "ERROR: %s\n", result.error.c_str());
        }
        return 2;
    }

    OutputPrinter::print(result, args.xmlOutput ? OutputFormat::Xml : OutputFormat::Text);
    return 0;
}

} // namespace openhi2txt
