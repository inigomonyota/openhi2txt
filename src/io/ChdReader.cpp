#include "io/ChdReader.h"

#include <libchdr/chd.h>

#include <algorithm>
#include <cstdio>
#include <limits>
#include <unordered_map>
#include <utility>

namespace openhi2txt {

namespace {

static std::FILE* openBinaryFile(const std::filesystem::path& path) {
#if defined(_WIN32)
    std::FILE* file = nullptr;
    return _wfopen_s(&file, path.c_str(), L"rb") == 0 ? file : nullptr;
#else
    return std::fopen(path.c_str(), "rb");
#endif
}

static std::string chdError(const std::string& action,
                            const std::filesystem::path& path,
                            chd_error error) {
    return action + " '" + path.string() + "': " + chd_error_string(error);
}

} // namespace

struct ChdReader::Impl {
    std::FILE* parentFile = nullptr;
    std::FILE* overlayFile = nullptr;
    chd_file* parent = nullptr;
    chd_file* overlay = nullptr;
    chd_file* active = nullptr;
    uint64_t logicalBytes = 0;
    std::unordered_map<uint32_t, std::vector<uint8_t>> hunkCache;

    ~Impl() {
        // libchdr's child handle owns its parent and chd_close() recursively
        // closes the complete chain. Closing both handles here double-frees the
        // parent whenever a DIF overlay is open.
        if (overlay) chd_close(overlay);
        else if (parent) chd_close(parent);
        if (overlayFile) std::fclose(overlayFile);
        if (parentFile) std::fclose(parentFile);
    }
};

ChdReader::ChdReader() = default;
ChdReader::~ChdReader() = default;
ChdReader::ChdReader(ChdReader&&) noexcept = default;
ChdReader& ChdReader::operator=(ChdReader&&) noexcept = default;

bool ChdReader::readHeader(const std::filesystem::path& path,
                           ChdHeaderInfo& info,
                           std::string& error) {
    info = {};
    error.clear();

    std::FILE* file = openBinaryFile(path);
    if (!file) {
        error = "Unable to open CHD '" + path.string() + "'";
        return false;
    }

    chd_header header{};
    const chd_error status = chd_read_header_file(file, &header);
    std::fclose(file);
    if (status != CHDERR_NONE) {
        error = chdError("Unable to read CHD header", path, status);
        return false;
    }

    std::copy_n(header.sha1, info.sha1.size(), info.sha1.begin());
    std::copy_n(header.parentsha1, info.parentSha1.size(), info.parentSha1.begin());
    info.hasParent = std::any_of(info.parentSha1.begin(), info.parentSha1.end(),
        [](uint8_t value) { return value != 0; });
    return true;
}

bool ChdReader::open(const std::filesystem::path& parentPath,
                     const std::filesystem::path& overlayPath,
                     std::string& error) {
    error.clear();
    impl_.reset();

    if (parentPath.empty()) {
        error = "A parent CHD path is required";
        return false;
    }

    auto next = std::make_unique<Impl>();
    next->parentFile = openBinaryFile(parentPath);
    if (!next->parentFile) {
        error = "Unable to open parent CHD '" + parentPath.string() + "'";
        return false;
    }

    chd_error status = chd_open_file(
        next->parentFile, CHD_OPEN_READ, nullptr, &next->parent);
    if (status != CHDERR_NONE) {
        error = chdError("Unable to open parent CHD", parentPath, status);
        return false;
    }

    next->active = next->parent;
    if (!overlayPath.empty()) {
        next->overlayFile = openBinaryFile(overlayPath);
        if (!next->overlayFile) {
            error = "Unable to open CHD overlay '" + overlayPath.string() + "'";
            return false;
        }
        status = chd_open_file(
            next->overlayFile, CHD_OPEN_READ, next->parent, &next->overlay);
        if (status != CHDERR_NONE) {
            error = chdError("Unable to open CHD overlay", overlayPath, status);
            return false;
        }
        next->active = next->overlay;
    }

    const chd_header* header = chd_get_header(next->active);
    if (!header || header->hunkbytes == 0) {
        error = "The selected CHD has an invalid or empty hunk size";
        return false;
    }

    next->logicalBytes = header->logicalbytes;
    impl_ = std::move(next);
    return true;
}

bool ChdReader::read(uint64_t offset,
                     size_t length,
                     std::vector<uint8_t>& output,
                     std::string& error) {
    output.clear();
    error.clear();

    if (!impl_ || !impl_->active) {
        error = "No CHD is open";
        return false;
    }
    if (offset > impl_->logicalBytes ||
        static_cast<uint64_t>(length) > impl_->logicalBytes - offset) {
        error = "Requested CHD byte range is outside the logical image";
        return false;
    }
    if (length == 0) return true;

    const chd_header* header = chd_get_header(impl_->active);
    const uint64_t hunkBytes = header->hunkbytes;
    const uint64_t firstHunk = offset / hunkBytes;
    const uint64_t finalByte = offset + static_cast<uint64_t>(length) - 1;
    const uint64_t lastHunk = finalByte / hunkBytes;
    if (lastHunk > std::numeric_limits<uint32_t>::max()) {
        error = "Requested CHD byte range exceeds libchdr's hunk index limit";
        return false;
    }

    output.reserve(length);

    for (uint64_t hunkIndex = firstHunk; hunkIndex <= lastHunk; ++hunkIndex) {
        const uint32_t index = static_cast<uint32_t>(hunkIndex);
        auto cached = impl_->hunkCache.find(index);
        if (cached == impl_->hunkCache.end()) {
            std::vector<uint8_t> hunk(header->hunkbytes);
            const chd_error status = chd_read(impl_->active, index, hunk.data());
            if (status != CHDERR_NONE) {
                error = "Unable to read CHD hunk " + std::to_string(hunkIndex) +
                    ": " + chd_error_string(status);
                output.clear();
                return false;
            }
            cached = impl_->hunkCache.emplace(index, std::move(hunk)).first;
        }
        const std::vector<uint8_t>& hunk = cached->second;

        const uint64_t hunkStart = hunkIndex * hunkBytes;
        const uint64_t copyStart = std::max(offset, hunkStart) - hunkStart;
        const uint64_t copyEnd = std::min(
            offset + static_cast<uint64_t>(length), hunkStart + hunkBytes) - hunkStart;
        output.insert(output.end(),
            hunk.begin() + static_cast<size_t>(copyStart),
            hunk.begin() + static_cast<size_t>(copyEnd));
    }

    return output.size() == length;
}

bool ChdReader::isOpen() const noexcept {
    return impl_ && impl_->active;
}

uint64_t ChdReader::logicalSize() const noexcept {
    return impl_ ? impl_->logicalBytes : 0;
}

} // namespace openhi2txt
