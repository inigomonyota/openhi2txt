#pragma once
#include <string>
#include <vector>
#include <filesystem>
#include "core/Types.h"

namespace openhi2txt::Utils {

std::string trim(std::string s);
bool ieq(std::string a, std::string b);

std::string basenameOf(const std::string& path);
int slashCount(const std::string& s);

int parseNum(const std::string& s);
bool parseBool(const std::string& s, bool defaultValue=false);
int64_t parseInt64Auto(const std::string& s, bool* ok=nullptr);
bool parseHexByte0x(const std::string& s, uint8_t& out);
bool parseHexNibble0x(const std::string& s, uint8_t& outNib);

std::string trimLeftBySet(const std::string& s, const std::string& chars);
std::string trimRightBySet(const std::string& s, const std::string& chars);

bool looksNumeric(const std::string& s);

std::string valueToString(const Value& v);
int64_t valueToInt(const Value& v);

bool readFileBytes(const std::filesystem::path& p, std::vector<uint8_t>& out);

// XML escape helper (preserve &name; entities)
void xmlEscapePrintPreserveEntities(const std::string& s);

} // namespace openhi2txt::Utils
