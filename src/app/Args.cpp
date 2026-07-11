#include "app/Args.h"
#include "io/Utils.h"
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <vector>

namespace openhi2txt {

namespace {

bool parseYesNo(const std::string& s, bool& out) {
    if (Utils::ieq(s, "yes") || Utils::ieq(s, "true") || s == "1") {
        out = true;
        return true;
    }
    if (Utils::ieq(s, "no") || Utils::ieq(s, "false") || s == "0") {
        out = false;
        return true;
    }
    return false;
}

std::pair<std::string, std::string> splitFieldValue(const std::string& s) {
    const size_t p = s.find(':');
    if (p == std::string::npos) return { s, "" };
    return { s.substr(0, p), s.substr(p + 1) };
}

std::filesystem::path inferMameRootFromInput(const std::filesystem::path& inputPath) {
    const auto parent = inputPath.parent_path();
    if (parent.empty()) return {};
    if (Utils::ieq(parent.filename().string(), "hiscore")) {
        return parent.parent_path();
    }
    return parent.parent_path();
}

std::string usage() {
    return
        "Usage: openhi2txt [-r|-ra|-rd|-v|-h] [-hiscoredat <path>] [-descr <path>] [-xml]\n"
        "                  [-keep-field <column>] [-keep-table-value <column:value>]\n"
        "                  [-keep-first-score <yes|no>] [-hide-column <column>]\n"
        "                  [-keep-first-table <yes|no>] [-max-lines <n>] [-max-columns <n>]\n"
        "                  [-score-grouping <yes|no>] [-score-grouping-separator <string>]\n"
        "                  [-score-grouping-size <n>] [-trace|-notrace]\n"
        "                  [--mame-root <path> --game <romname> | <hi_file_path>]";
}

} // namespace

Args Args::parse(int argc, char** argv) {
    Args a;
    for (int i = 1; i < argc; ++i) {
        std::string s = argv[i];
        auto next = [&](std::string& out) {
            if (i + 1 < argc) { out = argv[++i]; return true; }
            return false;
        };
        if (s == "-h" || s == "--help") {
            a.action = CliAction::Help;
        }
        else if (s == "-v" || s == "--version") {
            a.action = CliAction::Version;
        }
        else if (s == "-l" || s == "--list") {
            a.action = CliAction::List;
        }
        else if (s == "-location" || s == "--location") {
            a.showLocations = true;
        }
        else if (s == "--defs" || s == "-defs" || s == "-d" || s == "-descr" || s == "--descr" || s == "-ds" || s == "--ds") {
            std::string v; if (!next(v)) { a.error=s + " missing value"; return a; } a.defsZip=v;
        }
        else if (s == "-hiscoredat" || s == "--hiscoredat" || s == "-hs" || s == "--hs") {
            std::string v; if (!next(v)) { a.error=s + " missing value"; return a; } a.hiscoreDat=v;
        }
        else if (s == "--mame-root" || s == "-mame-root" || s == "-m") {
            std::string v; if (!next(v)) { a.error=s + " missing value"; return a; } a.mameRoot=v;
        }
        else if (s == "--game" || s == "-game" || s == "-g") {
            std::string v; if (!next(v)) { a.error=s + " missing value"; return a; } a.game=v;
        }
        else if (s == "-r" || s == "--read") { /* default read mode */ }
        else if (s == "-ra" || s == "--ra" || s == "--all") { a.showExtra = true; }
        else if (s == "-rd" || s == "--rd" || s == "--debug") { a.showExtra = true; a.showDebug = true; a.trace = true; }
        else if (s == "-xml" || s == "--xml") { a.xmlOutput = true; }
        else if (s == "-trace" || s == "--trace") { a.trace = true; }
        else if (s == "-notrace" || s == "--notrace") { a.trace = false; }
        else if (s == "-keep-field" || s == "--keep-field") {
            std::string v; if (!next(v)) { a.error=s + " missing value"; return a; } a.keepFields.push_back(v);
        }
        else if (s == "-hide-column" || s == "--hide-column" || s == "-hide-field" || s == "--hide-field") {
            std::string v; if (!next(v)) { a.error=s + " missing value"; return a; } a.hideFields.push_back(v);
        }
        else if (s == "-keep-table-value" || s == "--keep-table-value") {
            std::string v; if (!next(v)) { a.error=s + " missing value"; return a; } a.keepTableValues.push_back(splitFieldValue(v));
        }
        else if (s == "-keep-first-score" || s == "--keep-first-score") {
            std::string v; if (!next(v)) { a.error=s + " missing value"; return a; }
            if (!parseYesNo(v, a.keepFirstScore)) { a.error=s + " expects yes or no"; return a; }
        }
        else if (s == "-keep-first-table" || s == "--keep-first-table") {
            std::string v; if (!next(v)) { a.error=s + " missing value"; return a; }
            if (!parseYesNo(v, a.keepFirstTable)) { a.error=s + " expects yes or no"; return a; }
        }
        else if (s == "-max-lines" || s == "--max-lines") {
            std::string v; if (!next(v)) { a.error=s + " missing value"; return a; } a.maxLines = std::max(0, std::atoi(v.c_str()));
        }
        else if (s == "-max-columns" || s == "--max-columns") {
            std::string v; if (!next(v)) { a.error=s + " missing value"; return a; } a.maxColumns = std::max(0, std::atoi(v.c_str()));
        }
        else if (s == "-score-grouping" || s == "--score-grouping") {
            std::string v; if (!next(v)) { a.error=s + " missing value"; return a; }
            if (!parseYesNo(v, a.scoreGrouping)) { a.error=s + " expects yes or no"; return a; }
        }
        else if (s == "-score-grouping-separator" || s == "--score-grouping-separator") {
            std::string v; if (!next(v)) { a.error=s + " missing value"; return a; } a.scoreGroupingSeparator = v;
        }
        else if (s == "-score-grouping-size" || s == "--score-grouping-size") {
            std::string v; if (!next(v)) { a.error=s + " missing value"; return a; } a.scoreGroupingSize = std::max(1, std::atoi(v.c_str()));
        }
        else if (!s.empty() && s[0] == '-') {
            a.error = "ERROR: txt: unknown '" + s + "' parameter.";
            return a;
        }
        else {
            a.inputPath = s;
            if (a.game.empty()) a.game = std::filesystem::path(s).stem().string();
            if (a.mameRoot.empty()) a.mameRoot = inferMameRootFromInput(a.inputPath);
        }
    }
    if (a.action == CliAction::Help || a.action == CliAction::Version) {
        a.ok = true;
        return a;
    }
    if (a.defsZip.empty()) {
        a.error = usage();
        return a;
    }
    if (a.action == CliAction::Read && (a.mameRoot.empty() || a.game.empty())) {
        a.error = usage();
        return a;
    }
    a.ok = true;
    return a;
}

}
