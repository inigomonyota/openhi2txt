#include "io/HiscoreDat.h"
#include "io/Utils.h"

#include <fstream>
#include <sstream>
#include <unordered_set>

namespace openhi2txt::HiscoreDat {

    static inline std::string trim(const std::string& s) { return openhi2txt::Utils::trim(s); }
    static inline bool ieq(const std::string& a, const std::string& b) { return openhi2txt::Utils::ieq(a, b); }

    static bool isCommentLine(const std::string& t) {
        // hiscore.dat commonly uses ';' for comments; tolerate leading spaces.
        return !t.empty() && t[0] == ';';
    }

    static bool isLabelLine(const std::string& t, std::string& outLabel) {
        // label is "name:" possibly with trailing inline comment after it
        // examples:
        //   baracuda:
        //   baby2:  ; missing
        // We treat any token before ':' as label, even if "missing".
        const auto pos = t.find(':');
        if (pos == std::string::npos) return false;

        std::string left = trim(t.substr(0, pos));
        if (left.empty()) return false;

        // ignore weird directives that start with "@:" etc (those are body lines)
        if (left.size() >= 1 && left[0] == '@') return false;

        outLabel = left;
        return true;
    }

    Block findBlockForGame(const std::filesystem::path& hiscoreDatPath, const std::string& game) {
        Block empty{};

        std::ifstream f(hiscoreDatPath, std::ios::binary);
        if (!f) return empty;

        std::vector<std::string> pendingLabels;
        std::vector<std::string> currentBody;

        auto finalizeBlock = [&](std::vector<std::string>& labels, std::vector<std::string>& body) -> Block {
            Block b;
            b.labels = std::move(labels);
            b.bodyLines = std::move(body);
            return b;
            };

        auto blockHasGame = [&](const std::vector<std::string>& labels) -> bool {
            for (const auto& l : labels) if (ieq(l, game)) return true;
            return false;
            };

        std::string line;
        while (std::getline(f, line)) {
            // normalize CRLF
            if (!line.empty() && line.back() == '\r') line.pop_back();
            std::string t = trim(line);
            if (t.empty() || isCommentLine(t)) continue;

            std::string label;
            if (isLabelLine(t, label)) {
                // If we were collecting a body for the previous pendingLabels, a new label starts a new block.
                // Only finalize when we already started body collection (currentBody not empty).
                if (!currentBody.empty()) {
                    if (blockHasGame(pendingLabels)) {
                        return finalizeBlock(pendingLabels, currentBody);
                    }
                    pendingLabels.clear();
                    currentBody.clear();
                }
                pendingLabels.push_back(label);
                continue;
            }

            // Non-label, non-comment => body line.
            // This begins/continues the block body associated with pendingLabels.
            if (!pendingLabels.empty()) {
                currentBody.push_back(t);
            }
        }

        // EOF finalize
        if (!currentBody.empty() && blockHasGame(pendingLabels)) {
            return finalizeBlock(pendingLabels, currentBody);
        }

        return empty;
    }

    std::vector<std::string> aliasesForGame(const std::filesystem::path& hiscoreDatPath, const std::string& game) {
        Block b = findBlockForGame(hiscoreDatPath, game);
        if (b.labels.empty()) return {};

        // Return with game first (if present), then others in original order.
        std::vector<std::string> out;
        out.reserve(b.labels.size());

        int gamePos = -1;
        for (size_t i = 0; i < b.labels.size(); ++i) {
            if (ieq(b.labels[i], game)) { gamePos = (int)i; break; }
        }
        if (gamePos < 0) return {};

        out.push_back(b.labels[(size_t)gamePos]);
        for (size_t i = 0; i < b.labels.size(); ++i) {
            if ((int)i == gamePos) continue;
            out.push_back(b.labels[i]);
        }
        return out;
    }

} // namespace openhi2txt::HiscoreDat
