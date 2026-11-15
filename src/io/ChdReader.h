#pragma once

#include <cstddef>
#include <cstdint>
#include <array>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace openhi2txt {

struct ChdHeaderInfo {
    std::array<uint8_t, 20> sha1{};
    std::array<uint8_t, 20> parentSha1{};
    bool hasParent = false;
};

// Presents a CHD, optionally overlaid by a child/diff CHD, as one logical
// read-only byte sequence. CHD format details remain isolated in the
// implementation so XML and input selection code do not depend on libchdr.
class ChdReader {
public:
    ChdReader();
    ~ChdReader();

    ChdReader(ChdReader&&) noexcept;
    ChdReader& operator=(ChdReader&&) noexcept;

    ChdReader(const ChdReader&) = delete;
    ChdReader& operator=(const ChdReader&) = delete;

    bool open(const std::filesystem::path& parentPath,
              const std::filesystem::path& overlayPath,
              std::string& error);

    static bool readHeader(const std::filesystem::path& path,
                           ChdHeaderInfo& info,
                           std::string& error);

    bool read(uint64_t offset,
              size_t length,
              std::vector<uint8_t>& output,
              std::string& error);

    bool isOpen() const noexcept;
    uint64_t logicalSize() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace openhi2txt
