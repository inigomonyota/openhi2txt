#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace openhi2txt {
// The callback reads logical disk bytes, independent of CHD or any container.
using DiskRead = std::function<bool(uint64_t, size_t, std::vector<uint8_t>&, std::string&)>;
class FilesystemReader {
public:
    virtual ~FilesystemReader() = default;
    virtual bool readFile(const std::string& path, std::vector<uint8_t>& bytes,
                          std::string& error) = 0;
    static std::unique_ptr<FilesystemReader> open(const std::string& type,
        unsigned partition, uint64_t diskSize, DiskRead read, std::string& error);
};
}
