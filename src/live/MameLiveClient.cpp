#include "openhi2txt/mame_live.h"

#include "LiveProtocol.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <limits>
#include <optional>
#include <sstream>
#include <thread>
#include <utility>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace openhi2txt {
namespace {

// A maximum-size raw snapshot expands to twice its size in hexadecimal, plus
// its JSON envelope.
constexpr std::size_t kMaximumLineBytes = 65U * 1024U * 1024U;
constexpr auto kConnectDelay = std::chrono::milliseconds(250);

#ifdef _WIN32
using SocketHandle = SOCKET;
constexpr SocketHandle kInvalidSocket = INVALID_SOCKET;
void closeSocket(SocketHandle socket) { closesocket(socket); }
bool socketWouldBlock() {
    const int error = WSAGetLastError();
    return error == WSAEWOULDBLOCK || error == WSAEINPROGRESS;
}
bool setNonBlocking(SocketHandle socket) {
    u_long enabled = 1;
    return ioctlsocket(socket, FIONBIO, &enabled) == 0;
}
#else
using SocketHandle = int;
constexpr SocketHandle kInvalidSocket = -1;
void closeSocket(SocketHandle socket) { close(socket); }
bool socketWouldBlock() { return errno == EWOULDBLOCK || errno == EAGAIN || errno == EINPROGRESS; }
bool setNonBlocking(SocketHandle socket) {
    const int flags = fcntl(socket, F_GETFL, 0);
    return flags >= 0 && fcntl(socket, F_SETFL, flags | O_NONBLOCK) == 0;
}
#endif

class SocketGuard {
public:
    explicit SocketGuard(SocketHandle socket = kInvalidSocket) : socket_(socket) {}
    ~SocketGuard() { reset(); }
    SocketGuard(const SocketGuard&) = delete;
    SocketGuard& operator=(const SocketGuard&) = delete;
    SocketHandle get() const { return socket_; }
    explicit operator bool() const { return socket_ != kInvalidSocket; }
    SocketHandle release() {
        const SocketHandle socket = socket_;
        socket_ = kInvalidSocket;
        return socket;
    }
    void reset(SocketHandle socket = kInvalidSocket) {
        if (socket_ != kInvalidSocket) closeSocket(socket_);
        socket_ = socket;
    }
private:
    SocketHandle socket_;
};

bool waitWritable(SocketHandle socket, std::chrono::milliseconds timeout) {
    fd_set writable;
    FD_ZERO(&writable);
    FD_SET(socket, &writable);
    timeval value{};
    value.tv_sec = static_cast<long>(timeout.count() / 1000);
    value.tv_usec = static_cast<long>((timeout.count() % 1000) * 1000);
#ifdef _WIN32
    const int selected = select(0, nullptr, &writable, nullptr, &value);
#else
    const int selected = select(socket + 1, nullptr, &writable, nullptr, &value);
#endif
    if (selected <= 0) return false;
    int socketError = 0;
#ifdef _WIN32
    int length = sizeof(socketError);
#else
    socklen_t length = sizeof(socketError);
#endif
    return getsockopt(socket, SOL_SOCKET, SO_ERROR,
        reinterpret_cast<char*>(&socketError), &length) == 0 && socketError == 0;
}

SocketHandle connectLocal(std::uint16_t port) {
    SocketGuard socket(::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
    if (!socket || !setNonBlocking(socket.get())) return kInvalidSocket;
    sockaddr_in endpoint{};
    endpoint.sin_family = AF_INET;
    endpoint.sin_port = htons(port);
    endpoint.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    const int connected = ::connect(socket.get(), reinterpret_cast<sockaddr*>(&endpoint), sizeof(endpoint));
    if (connected != 0 && (!socketWouldBlock() || !waitWritable(socket.get(), kConnectDelay)))
        return kInvalidSocket;
    int enabled = 1;
    setsockopt(socket.get(), IPPROTO_TCP, TCP_NODELAY,
        reinterpret_cast<const char*>(&enabled), sizeof(enabled));
#if !defined(_WIN32) && defined(SO_NOSIGPIPE)
    setsockopt(socket.get(), SOL_SOCKET, SO_NOSIGPIPE, &enabled, sizeof(enabled));
#endif
    return socket.release();
}

bool identitiesEqual(const MameRuntimeIdentity& left, const MameRuntimeIdentity& right) {
    return left.machine == right.machine && left.softwareList == right.softwareList &&
        left.software == right.software;
}

std::string asciiLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

bool identityMatches(const MameRuntimeIdentity& actual,
                     const MameRuntimeIdentity& expected,
                     std::string& error) {
    if (!expected.machine.empty() && asciiLower(actual.machine) != asciiLower(expected.machine)) {
        error = "machine identity mismatch";
        return false;
    }
    if (!expected.softwareList.empty() &&
        asciiLower(actual.softwareList) != asciiLower(expected.softwareList)) {
        error = "software-list identity mismatch";
        return false;
    }
    if (!expected.software.empty() && asciiLower(actual.software) != asciiLower(expected.software)) {
        error = "software identity mismatch";
        return false;
    }
    return true;
}

struct LiveConfiguration {
    std::string definitionKey;
    std::string expectedSource;
    std::vector<live_detail::WatchRequest> watchRequests;
};

std::optional<LiveConfiguration> prepareConfiguration(
    Context& context,
    const MameLiveOptions& options,
    const MameRuntimeIdentity& identity,
    std::string& error) {
    LiveConfiguration configuration;
    const MameDefinitionResolution resolved = context.resolveDefinition(identity);
    if (!resolved.ok) {
        error = resolved.error;
        return std::nullopt;
    }
    configuration.definitionKey = resolved.definitionId;
    if (!options.expectedDefinition.empty() &&
        asciiLower(configuration.definitionKey) != asciiLower(options.expectedDefinition)) {
        error = "running MAME identity resolves to " + configuration.definitionKey +
            ", not expected definition " + options.expectedDefinition;
        return std::nullopt;
    }
    const HiScoreInputPlanResult plan = context.planGameInputs(identity);
    if (!plan.ok) {
        error = "no OpenHi2txt definition is available for " + configuration.definitionKey;
        return std::nullopt;
    }
    const bool hasHiInput = std::any_of(plan.inputs.begin(), plan.inputs.end(), [](const auto& input) {
        return input.fileKind.empty() || input.fileKind == "hi" || input.fileKind == ".hi";
    });
    if (hasHiInput) configuration.expectedSource = ".hi";
    else {
        for (const auto& input : plan.inputs) {
            const std::string& kind = input.fileKind;
            const bool logicalDisk = kind == "dif" || kind == "chd";
            const bool ordinaryNvram = !logicalDisk && input.acceptedBufferSizes.size() == 1;
            if (kind.empty() || kind == "hi" || kind == ".hi" || kind == "game" ||
                (!ordinaryNvram && (!logicalDisk || input.sourceWindowLength == 0)) ||
                input.watchRanges.empty()) continue;

            live_detail::WatchRequest request;
            request.source = kind;
            request.storage = logicalDisk ? "harddisk" : "nvram";
            request.sourceOffset = logicalDisk ? input.sourceWindowOffset : 0;
            request.sourceSize = logicalDisk ? input.sourceWindowLength : input.acceptedBufferSizes.front();
            for (const auto& range : input.watchRanges)
                request.ranges.push_back({range.offset, range.length});
            if (ordinaryNvram) {
                for (const auto& hint : options.storageHints) {
                    if (hint.size == request.sourceSize && asciiLower(hint.name) == asciiLower(kind))
                        request.storageNames.push_back(hint.name);
                }
                if (request.storageNames.empty()) {
                    for (const auto& hint : options.storageHints) {
                        if (hint.size == request.sourceSize &&
                            std::find(request.storageNames.begin(), request.storageNames.end(), hint.name) ==
                                request.storageNames.end()) request.storageNames.push_back(hint.name);
                    }
                }
            }
            configuration.watchRequests.push_back(std::move(request));
        }
    }
    if (configuration.expectedSource.empty() && configuration.watchRequests.empty()) {
        error = "live MAME does not support the input type required by " + configuration.definitionKey;
        return std::nullopt;
    }
    return configuration;
}

bool sourceReady(const std::string& source, const LiveConfiguration& configuration) {
    if (!configuration.expectedSource.empty()) return source == configuration.expectedSource;
    return std::any_of(configuration.watchRequests.begin(), configuration.watchRequests.end(),
        [&](const auto& request) { return request.source == source; });
}

} // namespace

struct MameLiveClient::Impl {
    Context& context;
    MameLiveOptions options;
    UpdateHandler updateHandler;
    DiagnosticHandler diagnosticHandler;
    std::atomic<bool> stopping{false};
    std::atomic<bool> active{false};
    std::thread worker;

    Impl(Context& contextValue, MameLiveOptions optionsValue,
         UpdateHandler updateValue, DiagnosticHandler diagnosticValue)
        : context(contextValue), options(std::move(optionsValue)),
          updateHandler(std::move(updateValue)), diagnosticHandler(std::move(diagnosticValue)) {}

    void diagnostic(MameLiveDiagnosticLevel level, const std::string& message) const {
        if (!diagnosticHandler) return;
        try { diagnosticHandler(level, message); }
        catch (...) {}
    }

    bool wait(std::chrono::milliseconds duration) const {
        constexpr auto quantum = std::chrono::milliseconds(25);
        while (duration > std::chrono::milliseconds::zero() && !stopping.load()) {
            const auto delay = (std::min)(duration, quantum);
            std::this_thread::sleep_for(delay);
            duration -= delay;
        }
        return stopping.load();
    }

    bool sendAll(SocketHandle socket, const std::string& data) const {
        std::size_t offset = 0;
        while (offset < data.size() && !stopping.load()) {
            const int remaining = static_cast<int>((std::min)(
                data.size() - offset, static_cast<std::size_t>((std::numeric_limits<int>::max)())));
#ifdef MSG_NOSIGNAL
            constexpr int sendFlags = MSG_NOSIGNAL;
#else
            constexpr int sendFlags = 0;
#endif
            const int sent = ::send(socket, data.data() + offset, remaining, sendFlags);
            if (sent > 0) offset += static_cast<std::size_t>(sent);
            else if (sent < 0 && socketWouldBlock()) wait(std::chrono::milliseconds(10));
            else return false;
        }
        return offset == data.size();
    }

    void deliver(const live_detail::Message& snapshot,
                 const LiveConfiguration& configuration,
                 bool& firstUpdate) {
        HiScoreResult result;
        const std::string sourceName = "mame://127.0.0.1/live/" +
            std::to_string(snapshot.session) + "/" + std::to_string(snapshot.sequence);
        if (!snapshot.ranges.empty()) {
            HiScoreSparseInput input;
            input.fileKind = snapshot.source;
            input.sourceOffset = snapshot.sourceOffset;
            input.sourceSize = snapshot.sourceSize;
            input.sourceName = sourceName;
            for (const auto& range : snapshot.ranges)
                input.ranges.push_back({range.offset, range.bytes});
            result = context.decodeSparseGame(snapshot.identity, {std::move(input)});
        }
        else {
            result = context.decodeGame(snapshot.identity,
                {HiScoreInput{snapshot.source, snapshot.bytes, sourceName}});
        }
        if (!result.ok) {
            diagnostic(MameLiveDiagnosticLevel::Warning,
                "OpenHi2txt rejected a live snapshot for " + configuration.definitionKey + ": " + result.error);
            return;
        }
        MameLiveUpdate update;
        update.identity = snapshot.identity;
        update.definitionKey = configuration.definitionKey;
        update.source = snapshot.source;
        update.session = snapshot.session;
        update.sequence = snapshot.sequence;
        update.reason = firstUpdate || snapshot.reason == "baseline"
            ? MameLiveUpdateReason::Baseline : MameLiveUpdateReason::Change;
        update.result = std::move(result);
        firstUpdate = false;
        try { updateHandler(std::move(update)); }
        catch (const std::exception& exception) {
            diagnostic(MameLiveDiagnosticLevel::Error,
                std::string("live update handler failed: ") + exception.what());
        }
        catch (...) {
            diagnostic(MameLiveDiagnosticLevel::Error, "live update handler failed");
        }
    }

    void run() {
#ifdef _WIN32
        WSADATA data{};
        if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
            diagnostic(MameLiveDiagnosticLevel::Error, "unable to initialize Windows sockets");
            active.store(false);
            return;
        }
#endif
        bool waitingReported = false;
        std::uint64_t lastSession = (std::numeric_limits<std::uint64_t>::max)();
        std::uint64_t lastSequence = (std::numeric_limits<std::uint64_t>::max)();
        bool firstUpdate = true;

        while (!stopping.load()) {
            SocketGuard socket(connectLocal(options.port));
            if (!socket) {
                if (!waitingReported) {
                    diagnostic(MameLiveDiagnosticLevel::Info,
                        "Waiting for MAME live scores on 127.0.0.1:" + std::to_string(options.port) + ".");
                    waitingReported = true;
                }
                if (wait(kConnectDelay)) break;
                continue;
            }
            waitingReported = false;
            diagnostic(MameLiveDiagnosticLevel::Info, "Connected to MAME live scores.");
            std::string input;
            std::optional<MameRuntimeIdentity> activeIdentity;
            std::optional<LiveConfiguration> configuration;
            std::optional<std::uint64_t> watchRequestSession;
            bool reconnect = false;

            while (!stopping.load() && !reconnect) {
                char buffer[8192];
                const int received = ::recv(socket.get(), buffer, sizeof(buffer), 0);
                if (received > 0) input.append(buffer, static_cast<std::size_t>(received));
                else if (received == 0) { reconnect = true; break; }
                else if (socketWouldBlock()) {
                    if (wait(std::chrono::milliseconds(20))) break;
                    continue;
                }
                else { reconnect = true; break; }

                if (input.size() > kMaximumLineBytes) {
                    diagnostic(MameLiveDiagnosticLevel::Warning, "Discarding an oversized live-score message.");
                    reconnect = true;
                    break;
                }
                std::size_t newline = 0;
                while ((newline = input.find('\n')) != std::string::npos) {
                    std::string line = input.substr(0, newline);
                    input.erase(0, newline + 1);
                    if (!line.empty() && line.back() == '\r') line.pop_back();
                    if (line.empty()) continue;

                    std::string error;
                    live_detail::Envelope envelope;
                    if (!live_detail::parseEnvelope(line, envelope, error) ||
                        !identityMatches(envelope.identity, options.expectedIdentity, error)) {
                        diagnostic(MameLiveDiagnosticLevel::Warning, "Ignoring live-score message: " + error + ".");
                        continue;
                    }
                    if (!activeIdentity || !identitiesEqual(*activeIdentity, envelope.identity)) {
                        configuration = prepareConfiguration(context, options, envelope.identity, error);
                        activeIdentity = envelope.identity;
                        watchRequestSession.reset();
                        if (!configuration) {
                            diagnostic(MameLiveDiagnosticLevel::Warning, "Ignoring MAME live session: " + error + ".");
                            continue;
                        }
                    }
                    if (!configuration) continue;
                    const bool ready = sourceReady(envelope.source, *configuration);
                    if (!ready && !configuration->watchRequests.empty() &&
                        watchRequestSession != envelope.session) {
                        if (!sendAll(socket.get(), live_detail::makeWatchRequest(
                                envelope.identity, configuration->watchRequests))) {
                            reconnect = true;
                            break;
                        }
                        watchRequestSession = envelope.session;
                    }
                    if (!ready || envelope.type == "hello") continue;

                    if (envelope.session == lastSession && envelope.sequence == lastSequence)
                        continue;

                    live_detail::Message snapshot;
                    if (!live_detail::parseMessage(line, configuration->expectedSource,
                            configuration->watchRequests, snapshot, error)) {
                        diagnostic(MameLiveDiagnosticLevel::Warning, "Ignoring live-score message: " + error + ".");
                        continue;
                    }
                    lastSession = snapshot.session;
                    lastSequence = snapshot.sequence;
                    deliver(snapshot, *configuration, firstUpdate);
                }
            }
            if (!stopping.load()) {
                diagnostic(MameLiveDiagnosticLevel::Info, "MAME live-score connection closed; reconnecting.");
                wait(std::chrono::milliseconds(100));
            }
        }
#ifdef _WIN32
        WSACleanup();
#endif
        active.store(false);
    }
};

MameLiveClient::MameLiveClient(Context& context, MameLiveOptions options,
                               UpdateHandler updateHandler, DiagnosticHandler diagnosticHandler)
    : impl_(std::make_unique<Impl>(context, std::move(options),
          std::move(updateHandler), std::move(diagnosticHandler))) {}

MameLiveClient::~MameLiveClient() { stop(); }

void MameLiveClient::start() {
    stop();
    impl_->stopping.store(false);
    impl_->active.store(true);
    try {
        impl_->worker = std::thread([implementation = impl_.get()] { implementation->run(); });
    }
    catch (...) {
        impl_->active.store(false);
        throw;
    }
}

void MameLiveClient::stop() {
    impl_->stopping.store(true);
    if (impl_->worker.joinable() && impl_->worker.get_id() != std::this_thread::get_id())
        impl_->worker.join();
}

bool MameLiveClient::running() const { return impl_->active.load(); }

} // namespace openhi2txt
