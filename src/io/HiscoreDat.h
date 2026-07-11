#pragma once
#include <filesystem>
#include <string>
#include <vector>

namespace openhi2txt::HiscoreDat {

    struct Block {
        std::vector<std::string> labels;     // e.g. baracuda, pacman, puckman...
        std::vector<std::string> bodyLines;  // non-comment, non-label lines until next label
    };

    // Returns the block that contains `game` (case-insensitive), else empty.
    Block findBlockForGame(const std::filesystem::path& hiscoreDatPath, const std::string& game);

    // Returns aliases in the block, starting with `game` if present, then the rest in file order.
    // If not found, returns empty.
    std::vector<std::string> aliasesForGame(const std::filesystem::path& hiscoreDatPath, const std::string& game);

} // namespace openhi2txt::HiscoreDat
