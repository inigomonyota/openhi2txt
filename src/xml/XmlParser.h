#pragma once
#include <string>
#include "core/Types.h"

namespace openhi2txt {

enum class XmlParseErrorKind {
    None,
    Syntax,
    Schema,
    VersionRequirement
};

struct XmlParseResult {
    GameDef def;
    bool ok = false;
    std::string error;
    XmlParseErrorKind errorKind = XmlParseErrorKind::None;
};

class XmlParser {
public:
    static GameDef parse(const std::string& xml);
    static XmlParseResult parseWithDiagnostics(const std::string& xml);
    static std::string getSameAsId(const std::string& xml);
    static std::string getRootName(const std::string& xml);
};

}
