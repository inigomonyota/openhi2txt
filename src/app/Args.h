#pragma once
#include <string>
#include <filesystem>
#include <utility>
#include <vector>

namespace openhi2txt {

enum class CliAction {
    Read,
    Help,
    Version,
    List
};

struct Args {
    std::filesystem::path defsZip = "./db";
    std::filesystem::path mameRoot;
    std::filesystem::path inputPath;
    std::filesystem::path hiscoreDat;
    std::string game;
    CliAction action = CliAction::Read;
    bool showLocations = false;

    bool ok = false;
    std::string error;

	bool showExtra = false;
	bool showDebug = false;
    bool xmlOutput = false;
    bool trace = false;

    std::vector<std::string> keepFields;
    std::vector<std::string> hideFields;
    std::vector<std::pair<std::string, std::string>> keepTableValues;
    bool keepFirstScore = false;
    bool keepFirstTable = false;
    int maxLines = 0;
    int maxColumns = 0;

    bool scoreGrouping = false;
    std::string scoreGroupingSeparator = ".";
    int scoreGroupingSize = 3;

    static Args parse(int argc, char** argv);
};

}
