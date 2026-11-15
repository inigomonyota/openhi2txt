#include "openhi2txt/openhi2txt.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

namespace {

int failures = 0;

void expect(bool condition, const std::string& message) {
    if (condition) return;
    std::cerr << "FAILED: " << message << '\n';
    ++failures;
}

void writeText(const std::filesystem::path& path, const std::string& contents) {
    std::ofstream file(path, std::ios::binary);
    file << contents;
}

void writeBytes(const std::filesystem::path& path) {
    std::ofstream file(path, std::ios::binary);
    const char bytes[] = {0x00, 0x2a};
    file.write(bytes, sizeof(bytes));
}

} // namespace

int main() {
    using namespace openhi2txt;

#ifdef _WIN32
    const int processId = _getpid();
#else
    const int processId = getpid();
#endif
    const std::filesystem::path root = std::filesystem::temp_directory_path() /
        ("openhi2txt-identity-" + std::to_string(processId));
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root / "definitions", error);
    std::filesystem::create_directories(root / "mame" / "nvram" / "genesis", error);

    const std::string definition =
        "<!DOCTYPE openhi2txt><openhi2txt requires=\"" + std::string(VersionString) +
        "\" id=\"genesis_tecmobb\">"
        "<identity type=\"mame\" machine=\"genesis\" software-list=\"megadriv\" "
        "software=\"tecmobb\"/>"
        "<structure file=\"sram\"><check><size>2</size></check>"
        "<elt size=\"2\" type=\"int\" id=\"SCORE\"/></structure>"
        "<output><table><column id=\"SCORE\"/></table></output>"
        "</openhi2txt>";
    writeText(root / "definitions" / "arbitrary-file-name.xml", definition);
    writeBytes(root / "mame" / "nvram" / "genesis" / "tecmobb.nv");

    ContextOptions options;
    options.definitionsZip = (root / "definitions").string();
    options.mameRoot = (root / "mame").string();
    Context context(std::move(options));

    const MameRuntimeIdentity identity{"genesis", "megadriv", "tecmobb"};
    const MameDefinitionResolution resolved = context.resolveDefinition(identity);
    expect(resolved.ok, "structured MAME identity resolves successfully");
    expect(resolved.definitionId == "genesis_tecmobb",
           "the declared definition ID is returned");
    expect(resolved.definitionEntry == "arbitrary-file-name",
           "identity resolution does not depend on the definition filename");

    context.prepareMameDefinitionIndex();
    const MameDefinitionResolution cached = context.resolveDefinition(identity);
    expect(cached.ok && cached.definitionId == resolved.definitionId &&
               cached.definitionEntry == resolved.definitionEntry,
           "prepared identity resolution remains stable and cached");

    const HiScoreResult decoded = context.refreshGame(identity);
    expect(decoded.ok, "software battery input decodes through structured identity");
    expect(decoded.game == "genesis_tecmobb",
           "decoded results use the stable definition ID");
    expect(decoded.usedInputPath.find("genesis") != std::string::npos &&
           decoded.usedInputPath.find("tecmobb.nv") != std::string::npos,
           "static software input uses nvram/<machine>/<software>.nv");
    expect(!decoded.tables.empty() && !decoded.tables.front().rows.empty(),
           "the battery input produces a display table");

    const MameDefinitionResolution missing = context.resolveDefinition(
        MameRuntimeIdentity{"genesis", "megadriv", "missing"});
    expect(!missing.ok && missing.errorKind == HiScoreErrorKind::DefinitionNotFound,
           "an unknown structured identity reports a missing definition");

    const MameDefinitionResolution unsafe = context.resolveDefinition(
        MameRuntimeIdentity{"..", "megadriv", "tecmobb"});
    expect(!unsafe.ok && unsafe.errorKind == HiScoreErrorKind::InvalidData,
           "identity components cannot escape MAME's storage directories");

    std::filesystem::remove_all(root, error);
    if (failures != 0) {
        std::cerr << failures << " test(s) failed.\n";
        return 1;
    }
    std::cout << "All identity resolution tests passed.\n";
    return 0;
}
