#pragma once

#include "openhi2txt/openhi2txt.h"

#include <cstdint>
#include <string>
#include <vector>

namespace openhi2txt::live_detail {

struct WatchRange {
    std::uint64_t offset = 0;
    std::uint64_t length = 0;
};

struct WatchRequest {
    std::string source;
    std::string storage;
    std::vector<std::string> storageNames;
    std::uint64_t sourceOffset = 0;
    std::uint64_t sourceSize = 0;
    std::vector<WatchRange> ranges;
};

struct SnapshotRange {
    std::uint64_t offset = 0;
    std::vector<std::uint8_t> bytes;
};

struct Message {
    std::string type;
    MameRuntimeIdentity identity;
    std::string source;
    std::uint64_t session = 0;
    std::uint64_t sequence = 0;
    std::string reason;
    std::uint64_t sourceOffset = 0;
    std::uint64_t sourceSize = 0;
    std::vector<std::uint8_t> bytes;
    std::vector<SnapshotRange> ranges;
};

struct Envelope {
    std::string type;
    MameRuntimeIdentity identity;
    std::string source;
    std::uint64_t session = 0;
    std::uint64_t sequence = 0;
};

bool parseEnvelope(const std::string& line, Envelope& envelope, std::string& error);

bool parseMessage(const std::string& line,
                  const std::string& expectedSource,
                  const std::vector<WatchRequest>& watchRequests,
                  Message& message,
                  std::string& error);

std::string makeWatchRequest(const MameRuntimeIdentity& identity,
                             const std::vector<WatchRequest>& requests);

} // namespace openhi2txt::live_detail
