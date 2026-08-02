#include "openhi2txt/openhi2txt.h"
#include "xml/XmlParser.h"

#include <iostream>
#include <string>

namespace {

int failures = 0;

void expect(bool condition, const std::string& message) {
    if (condition) return;
    std::cerr << "FAILED: " << message << '\n';
    ++failures;
}

std::string openDefinition(const std::string& required, const std::string& content = {}) {
    return "<?xml version=\"1.0\"?>\n"
           "<!DOCTYPE openhi2txt>\n"
           "<openhi2txt requires=\"" + required +
           "\" label=\"Test\" ingame-score=\"true\">" + content + "</openhi2txt>";
}

} // namespace

int main() {
    using namespace openhi2txt;

    const std::string legacy =
        "<?xml version=\"1.0\"?>\n"
        "<!DOCTYPE hi2txt SYSTEM \"hi2txt.dtd\">\n"
        "<hi2txt label=\"Legacy\" ingame-score=\"false\"></hi2txt>";
    expect(XmlParser::parseWithDiagnostics(legacy).ok,
           "the legacy hi2txt root remains supported");

    const auto current = XmlParser::parseWithDiagnostics(openDefinition(VersionString));
    expect(current.ok, "the openhi2txt root accepts the running version");

    const auto older = XmlParser::parseWithDiagnostics(openDefinition("0.0.0"));
    expect(older.ok, "the openhi2txt root accepts an older minimum version");

    const std::string sameAsXml = openDefinition(VersionString, "<sameas id=\"pacman\"/>");
    expect(XmlParser::parseWithDiagnostics(sameAsXml).ok,
           "the openhi2txt root accepts existing hi2txt children");
    expect(XmlParser::getSameAsId(sameAsXml) == "pacman",
           "sameas resolution supports the openhi2txt root");

    const std::string futureVersion = std::to_string(VersionMajor) + "." +
        std::to_string(VersionMinor) + "." + std::to_string(VersionPatch + 1);
    const auto future = XmlParser::parseWithDiagnostics(openDefinition(futureVersion));
    expect(!future.ok, "a newer required openhi2txt version is rejected");
    expect(future.errorKind == XmlParseErrorKind::VersionRequirement,
           "a newer version reports a version-requirement error");
    expect(future.error.find(futureVersion) != std::string::npos &&
           future.error.find(VersionString) != std::string::npos,
           "the version error names both required and running versions");

    const auto missing = XmlParser::parseWithDiagnostics(
        "<!DOCTYPE openhi2txt><openhi2txt></openhi2txt>");
    expect(!missing.ok && missing.errorKind == XmlParseErrorKind::VersionRequirement,
           "requires is mandatory on the openhi2txt root");

    const auto malformed = XmlParser::parseWithDiagnostics(openDefinition("1.0"));
    expect(!malformed.ok && malformed.errorKind == XmlParseErrorKind::VersionRequirement,
           "requires must use MAJOR.MINOR.PATCH");

    std::string unknownAttributeXml = openDefinition(VersionString);
    unknownAttributeXml.insert(unknownAttributeXml.find(" label="), " unknown=\"value\"");
    const auto unknownAttribute = XmlParser::parseWithDiagnostics(unknownAttributeXml);
    expect(!unknownAttribute.ok && unknownAttribute.errorKind == XmlParseErrorKind::Schema,
           "openhi2txt currently accepts only inherited root attributes plus requires");

    if (failures != 0) {
        std::cerr << failures << " test(s) failed.\n";
        return 1;
    }

    std::cout << "All XML parser tests passed.\n";
    return 0;
}
