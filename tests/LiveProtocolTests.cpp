#include "live/LiveProtocol.h"

#include <iostream>
#include <string>
#include <vector>

namespace {
int failures = 0;

void expect(bool condition, const std::string& message) {
    if (condition) return;
    std::cerr << "FAILED: " << message << '\n';
    ++failures;
}
}

int main() {
    using namespace openhi2txt;
    using namespace openhi2txt::live_detail;

    const std::string hello = R"({"identity":{"software":"sor2u","machine":"genesis","softwareList":"megadriv"},"protocolVersion":1,"session":4,"source":".hi","type":"hello","protocol":"openhi2txt-live"})";
    Envelope envelope;
    std::string error;
    expect(parseEnvelope(hello, envelope, error), "hello parses");
    expect(envelope.identity.machine == "genesis" && envelope.identity.software == "sor2u",
        "hello retains authoritative identity");
    expect(envelope.source == ".hi" && envelope.session == 4, "hello retains source session");

    const std::vector<WatchRequest> requests{{
        "sram", "nvram", {"sram"}, 0, 16384, {{1157, 4}, {2000, 2}}
    }};
    const std::string request = makeWatchRequest(envelope.identity, requests);
    expect(request.find("\"machine\":\"genesis\"") != std::string::npos,
        "watch request carries MAME identity");
    expect(request.find("\"storageNames\":[\"sram\"]") != std::string::npos,
        "watch request carries storage hints");

    const std::string snapshot = R"({"protocol":"openhi2txt-live","protocolVersion":1,"type":"snapshot","identity":{"machine":"genesis","softwareList":"megadriv","software":"sor2u"},"source":"sram","session":5,"sequence":1,"reason":"baseline","encoding":"hex","sourceOffset":0,"sourceSize":16384,"ranges":[{"offset":1157,"length":4,"data":"0102a0ff"},{"offset":2000,"length":2,"data":"1020"}]})";
    Envelope snapshotEnvelope;
    error.clear();
    expect(parseEnvelope(snapshot, snapshotEnvelope, error) && snapshotEnvelope.sequence == 1,
        "snapshot envelope exposes its sequence before payload decoding");
    Message parsed;
    error.clear();
    expect(parseMessage(snapshot, "", requests, parsed, error), "sparse snapshot parses");
    expect(parsed.ranges.size() == 2 && parsed.ranges[0].bytes[2] == 0xa0,
        "sparse snapshot data is decoded");

    const std::string badSnapshot = R"({"protocol":"openhi2txt-live","protocolVersion":1,"type":"snapshot","identity":{"machine":"genesis","softwareList":"megadriv","software":"sor2u"},"source":"sram","session":5,"sequence":2,"reason":"change","encoding":"hex","sourceOffset":0,"sourceSize":16384,"ranges":[{"offset":1158,"length":4,"data":"0102a0ff"},{"offset":2000,"length":2,"data":"1020"}]})";
    Message rejected;
    error.clear();
    expect(!parseMessage(badSnapshot, "", requests, rejected, error),
        "snapshot with an unrequested range is rejected");

    if (failures) return 1;
    std::cout << "All MAME live protocol tests passed.\n";
    return 0;
}
