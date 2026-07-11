// ArchiveManager.cpp (replacement)
//
// Fixes:
// - Try exact top-level entries first; this keeps one-shot CLI calls fast.
// - Fall back to a per-process zip index for nested or case-varied entries.
// - Index all entries by basename (case-insensitive) and store "best" candidate per basename.

#include "io/ArchiveManager.h"
#include "io/Utils.h"

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>
#include <algorithm>
#include <mutex>

#include <zlib.h>
#include "minizip/unzip.h"

namespace fs = std::filesystem;

namespace openhi2txt {

    static std::string toLowerAscii(std::string s) {
        for (char& c : s) {
            if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        }
        return s;
    }

    static bool entryBetterThan(const std::string& a, const std::string& b) {
        // - lexicographic tie-break
        if (a.size() != b.size()) return a.size() < b.size();
        int sa = Utils::slashCount(a), sb = Utils::slashCount(b);
        if (sa != sb) return sa < sb;
        return a < b;
    }

    struct ZipIndex {
        fs::path zipPath;
        std::uintmax_t fileSize = 0;
        fs::file_time_type mtime{};

        // basename(lower) -> best full entry name
        std::unordered_map<std::string, std::string> bestByBasename;
    };

    static std::unordered_map<std::string, ZipIndex> g_indexes;
    static std::mutex g_indexMutex;

    static bool buildZipIndexLocked(const fs::path& zipPath, ZipIndex& idx) {
        idx = {};
        idx.zipPath = zipPath;

        std::error_code ec;
        idx.fileSize = fs::file_size(zipPath, ec);
        if (ec) idx.fileSize = 0;
        idx.mtime = fs::last_write_time(zipPath, ec);
        if (ec) idx.mtime = fs::file_time_type{};

        unzFile uf = unzOpen64(zipPath.string().c_str());
        if (!uf) return false;
        if (unzGoToFirstFile(uf) != UNZ_OK) { unzClose(uf); return false; }

        do {
            char name[2048]{};
            unz_file_info64 info{};
            if (unzGetCurrentFileInfo64(uf, &info, name, sizeof(name), 0, 0, 0, 0) != UNZ_OK)
                continue;

            std::string full = name;
            std::string base = Utils::basenameOf(full);
            std::string key = toLowerAscii(base);

            auto it = idx.bestByBasename.find(key);
            if (it == idx.bestByBasename.end()) {
                idx.bestByBasename.emplace(std::move(key), std::move(full));
            }
            else {
                if (entryBetterThan(full, it->second)) it->second = std::move(full);
            }
        } while (unzGoToNextFile(uf) == UNZ_OK);

        unzClose(uf);
        return !idx.bestByBasename.empty();
    }

    static bool indexNeedsRebuild(const fs::path& zipPath, const ZipIndex* idx) {
        std::error_code ec;
        const auto sz = fs::file_size(zipPath, ec);
        const auto mt = fs::last_write_time(zipPath, ec);

        return idx == nullptr ||
               idx->zipPath.empty() ||
               (ec ? true : (idx->fileSize != sz || idx->mtime != mt));
    }

    static std::string zipCacheKey(const fs::path& zipPath) {
        return toLowerAscii(zipPath.lexically_normal().string());
    }

    static bool ensureZipIndexLocked(const fs::path& zipPath) {
        const std::string key = zipCacheKey(zipPath);
        auto it = g_indexes.find(key);
        ZipIndex* idx = it == g_indexes.end() ? nullptr : &it->second;

        if (!indexNeedsRebuild(zipPath, idx)) return true;

        ZipIndex rebuilt;
        if (!buildZipIndexLocked(zipPath, rebuilt)) return false;
        g_indexes[key] = std::move(rebuilt);
        return true;
    }

    static bool zipExtractByEntryName(const fs::path& zipPath, const std::string& entryName, std::string& out) {
        unzFile uf = unzOpen64(zipPath.string().c_str());
        if (!uf) return false;

        // Use case-sensitive match; our index stored exact entry name.
        if (unzLocateFile(uf, entryName.c_str(), 0) != UNZ_OK) { unzClose(uf); return false; }

        if (unzOpenCurrentFile(uf) != UNZ_OK) { unzClose(uf); return false; }

        out.clear();
        std::vector<char> buf(64 * 1024);
        for (;;) {
            int n = unzReadCurrentFile(uf, buf.data(), (unsigned)buf.size());
            if (n < 0) { unzCloseCurrentFile(uf); unzClose(uf); return false; }
            if (n == 0) break;
            out.append(buf.data(), buf.data() + n);
        }

        unzCloseCurrentFile(uf);
        unzClose(uf);
        return true;
    }

    static bool extensionMatches(const std::string& base, const std::string& ext) {
        if (ext.empty()) return true;
        if (base.size() < ext.size()) return false;
        return base.compare(base.size() - ext.size(), ext.size(), ext) == 0;
    }

    static bool readCurrentZipFile(unzFile uf, std::string& out) {
        if (unzOpenCurrentFile(uf) != UNZ_OK) return false;

        out.clear();
        std::vector<char> buf(64 * 1024);
        for (;;) {
            int n = unzReadCurrentFile(uf, buf.data(), (unsigned)buf.size());
            if (n < 0) {
                unzCloseCurrentFile(uf);
                return false;
            }
            if (n == 0) break;
            out.append(buf.data(), buf.data() + n);
        }

        unzCloseCurrentFile(uf);
        return true;
    }

    static bool readDirectoryEntry(const fs::path& dirPath, const std::string& wantBase, std::string& outXml) {
        std::error_code ec;
        const fs::path exact = dirPath / wantBase;
        if (fs::is_regular_file(exact, ec)) {
            std::vector<uint8_t> bytes;
            if (!Utils::readFileBytes(exact, bytes)) return false;
            outXml.assign(bytes.begin(), bytes.end());
            return true;
        }

        const std::string wanted = toLowerAscii(wantBase);
        fs::path best;
        for (fs::recursive_directory_iterator it(dirPath, ec), end; !ec && it != end; it.increment(ec)) {
            if (!it->is_regular_file(ec)) continue;
            const fs::path p = it->path();
            if (toLowerAscii(p.filename().string()) != wanted) continue;
            if (best.empty() || entryBetterThan(p.generic_string(), best.generic_string())) {
                best = p;
            }
        }

        if (best.empty()) return false;
        std::vector<uint8_t> bytes;
        if (!Utils::readFileBytes(best, bytes)) return false;
        outXml.assign(bytes.begin(), bytes.end());
        return true;
    }

    bool ArchiveManager::extractBest(const fs::path& zipPath, const std::string& wantBase, std::string& outXml) {
        std::error_code ec;
        if (fs::is_directory(zipPath, ec)) {
            return readDirectoryEntry(zipPath, wantBase, outXml);
        }

        if (zipExtractByEntryName(zipPath, wantBase, outXml)) {
            return true;
        }

        const std::string key = toLowerAscii(wantBase);

        std::string entry;
        {
            std::lock_guard<std::mutex> lock(g_indexMutex);
            if (!ensureZipIndexLocked(zipPath)) return false;

            auto idxIt = g_indexes.find(zipCacheKey(zipPath));
            if (idxIt == g_indexes.end()) return false;

            auto it = idxIt->second.bestByBasename.find(key);
            if (it == idxIt->second.bestByBasename.end()) return false;
            entry = it->second;
        }

        return zipExtractByEntryName(zipPath, entry, outXml);
    }

    std::vector<ArchiveEntry> ArchiveManager::extractAllBest(const fs::path& zipPath,
                                                             const std::string& extension) {
        std::vector<ArchiveEntry> result;
        const std::string ext = toLowerAscii(extension);

        std::error_code ec;
        if (fs::is_directory(zipPath, ec)) {
            std::unordered_map<std::string, std::pair<std::string, std::string>> best;
            for (fs::recursive_directory_iterator it(zipPath, ec), end; !ec && it != end; it.increment(ec)) {
                if (!it->is_regular_file(ec)) continue;

                const fs::path p = it->path();
                std::string base = toLowerAscii(p.filename().string());
                if (!extensionMatches(base, ext)) continue;

                std::vector<uint8_t> bytes;
                if (!Utils::readFileBytes(p, bytes)) continue;

                const std::string full = p.generic_string();
                auto bestIt = best.find(base);
                if (bestIt == best.end() || entryBetterThan(full, bestIt->second.first)) {
                    best[base] = { full, std::string(bytes.begin(), bytes.end()) };
                }
            }

            result.reserve(best.size());
            for (auto& kv : best) {
                result.push_back(ArchiveEntry{ kv.first, std::move(kv.second.second) });
            }
            std::sort(result.begin(), result.end(),
                [](const ArchiveEntry& a, const ArchiveEntry& b) { return a.basename < b.basename; });
            return result;
        }

        unzFile uf = unzOpen64(zipPath.string().c_str());
        if (!uf) return result;
        if (unzGoToFirstFile(uf) != UNZ_OK) {
            unzClose(uf);
            return result;
        }

        std::unordered_map<std::string, std::pair<std::string, std::string>> best;
        do {
            char name[2048]{};
            unz_file_info64 info{};
            if (unzGetCurrentFileInfo64(uf, &info, name, sizeof(name), 0, 0, 0, 0) != UNZ_OK) {
                continue;
            }

            std::string full = name;
            std::string base = toLowerAscii(Utils::basenameOf(full));
            if (!extensionMatches(base, ext)) continue;

            auto bestIt = best.find(base);
            if (bestIt != best.end() && !entryBetterThan(full, bestIt->second.first)) {
                continue;
            }

            std::string content;
            if (!readCurrentZipFile(uf, content)) continue;
            best[base] = { std::move(full), std::move(content) };
        } while (unzGoToNextFile(uf) == UNZ_OK);

        unzClose(uf);

        result.reserve(best.size());
        for (auto& kv : best) {
            result.push_back(ArchiveEntry{ kv.first, std::move(kv.second.second) });
        }
        std::sort(result.begin(), result.end(),
            [](const ArchiveEntry& a, const ArchiveEntry& b) { return a.basename < b.basename; });
        return result;
    }

    std::vector<std::string> ArchiveManager::listBasenames(const fs::path& zipPath,
                                                           const std::string& extension) {
        std::vector<std::string> result;
        const std::string ext = toLowerAscii(extension);

        std::error_code ec;
        if (fs::is_directory(zipPath, ec)) {
            for (fs::recursive_directory_iterator it(zipPath, ec), end; !ec && it != end; it.increment(ec)) {
                if (!it->is_regular_file(ec)) continue;
                std::string base = toLowerAscii(it->path().filename().string());
                if (!extensionMatches(base, ext)) continue;
                result.push_back(std::move(base));
            }
            std::sort(result.begin(), result.end());
            result.erase(std::unique(result.begin(), result.end()), result.end());
            return result;
        }

        {
            std::lock_guard<std::mutex> lock(g_indexMutex);
            if (!ensureZipIndexLocked(zipPath)) return result;

            auto idxIt = g_indexes.find(zipCacheKey(zipPath));
            if (idxIt == g_indexes.end()) return result;

            result.reserve(idxIt->second.bestByBasename.size());
            for (const auto& kv : idxIt->second.bestByBasename) {
                const std::string& base = kv.first;
                if (!extensionMatches(base, ext)) continue;
                result.push_back(base);
            }
        }

        std::sort(result.begin(), result.end());
        return result;
    }

} // namespace openhi2txt
