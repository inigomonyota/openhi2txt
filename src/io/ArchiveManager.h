#pragma once
#include <string>
#include <vector>
#include <filesystem>

namespace openhi2txt {

struct ArchiveEntry {
    std::string basename;
    std::string content;
};

class ArchiveManager {
public:
    static bool extractBest(const std::filesystem::path& zipPath, const std::string& wantBase, std::string& outXml);
    static std::vector<ArchiveEntry> extractAllBest(const std::filesystem::path& zipPath,
                                                    const std::string& extension = {});
    static std::vector<std::string> listBasenames(const std::filesystem::path& zipPath,
                                                  const std::string& extension = {});
};

}
