#pragma once

#include "openhi2txt.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace openhi2txt {

struct MameLiveStorageHint {
    std::string name;
    std::uint64_t size = 0;
};

struct MameLiveOptions {
    // Safety check supplied by the embedding application.  Leave empty when
    // any definition reported by MAME is acceptable.
    std::string expectedDefinition;

    // Optional launch-time expectations. Empty fields are not checked; MAME's
    // hello remains authoritative for definition selection.
    MameRuntimeIdentity expectedIdentity;
    std::vector<MameLiveStorageHint> storageHints;
    std::uint16_t port = 32123;
};

enum class MameLiveUpdateReason {
    Baseline,
    Change
};

struct MameLiveUpdate {
    MameRuntimeIdentity identity;
    std::string definitionKey;
    std::string source;
    std::uint64_t session = 0;
    std::uint64_t sequence = 0;
    MameLiveUpdateReason reason = MameLiveUpdateReason::Change;
    HiScoreResult result;
};

enum class MameLiveDiagnosticLevel {
    Info,
    Warning,
    Error
};

class MameLiveClient {
public:
    using UpdateHandler = std::function<void(MameLiveUpdate)>;
    using DiagnosticHandler = std::function<void(MameLiveDiagnosticLevel, const std::string&)>;

    MameLiveClient(Context& context,
                   MameLiveOptions options,
                   UpdateHandler updateHandler,
                   DiagnosticHandler diagnosticHandler = {});
    ~MameLiveClient();

    MameLiveClient(const MameLiveClient&) = delete;
    MameLiveClient& operator=(const MameLiveClient&) = delete;

    void start();
    void stop();
    bool running() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace openhi2txt
