#include "xml/XmlParser.h"
#include "xml/EntityMapper.h"
#include "io/Utils.h"

#include <sstream>
#include <algorithm>
#include <cctype>
#include <unordered_map>
#include <unordered_set>

#include "rapidxml.hpp"

namespace openhi2txt {

	using namespace Utils;

	static std::string attr(rapidxml::xml_node<>* n, const char* name) {
		if (!n) return "";
		if (auto* a = n->first_attribute(name)) {
			std::string val = a->value() ? a->value() : "";
			return EntityMapper::resolve(trim(val));
		}
		return "";
	}

	static std::string attrRaw(rapidxml::xml_node<>* n, const char* name) {
		if (!n) return "";
		if (auto* a = n->first_attribute(name)) {
			std::string val = a->value() ? a->value() : "";
			return EntityMapper::resolve(val);
		}
		return "";
	}

	static std::string nodeText(rapidxml::xml_node<>* n) {
		if (!n) return "";
		std::string result;
		for (auto* c = n->first_node(); c; c = c->next_sibling()) {
			if (c->type() == rapidxml::node_data || c->type() == rapidxml::node_cdata)
				result += (c->value() ? c->value() : "");
		}
		if (result.empty() && n->value()) result = n->value();
		return EntityMapper::resolve(trim(result));
	}

	static std::string nodeTextRaw(rapidxml::xml_node<>* n) {
		if (!n) return "";
		std::string result;
		for (auto* c = n->first_node(); c; c = c->next_sibling()) {
			if (c->type() == rapidxml::node_data || c->type() == rapidxml::node_cdata)
				result += (c->value() ? c->value() : "");
		}
		if (result.empty() && n->value()) result = n->value();
		return EntityMapper::resolve(result);
	}

	static std::string formatAttr(rapidxml::xml_node<>* n, const char* name = "format") {
		return attrRaw(n, name);
	}

	static std::string parseErrorWithLocation(const std::string& message, const char* begin, const char* where) {
		if (!begin || !where || where < begin) return message;

		int line = 1;
		int col = 1;
		for (const char* p = begin; p < where && *p; ++p) {
			if (*p == '\n') {
				++line;
				col = 1;
			}
			else {
				++col;
			}
		}

		std::stringstream ss;
		ss << message << " Line " << line << ", position " << col << ".";
		return ss.str();
	}

	static const std::unordered_map<std::string, std::unordered_set<std::string>>& allowedAttrsByElement() {
		static const std::unordered_map<std::string, std::unordered_set<std::string>> allowed = {
			{ "hi2txt", { "label", "ingame-score" } },
			{ "structure", { "file", "output", "byte-swap" } },
			{ "check", {} },
			{ "definition", { "offset" } },
			{ "size", {} },
			{ "loop", { "count", "start", "step", "skip-last-bytes", "skip-first-bytes" } },
			{ "elt", { "size", "type", "id", "charset", "base", "table-index", "decoding-profile", "ascii-offset",
				"endianness", "swap-skip-order", "byte-skip", "byte-trim", "byte-trunc", "nibble-skip",
				"nibble-trim", "byte-swap", "bit-swap", "table-index-format", "bitmask", "src-unit-size",
				"dst-unit-size", "ascii-step", "format", "offset" } },
			{ "output", { "id" } },
			{ "table", { "id", "line-ignore", "line-ignore-operator", "sort", "sort-order", "sort-format", "lines-max", "display" } },
			{ "column", { "id", "src", "format", "display" } },
			{ "field", { "id", "src", "format", "display" } },
			{ "txt", {} },
			{ "format", { "id", "formatter", "apply-to", "input-as-subcolumns-input" } },
			{ "add", {} },
			{ "increment", {} },
			{ "prefix", { "empty", "consume" } },
			{ "multiply", {} },
			{ "divide", {} },
			{ "suffix", { "empty", "consume" } },
			{ "postfix", { "empty", "consume" } },
			{ "sum", {} },
			{ "concat", {} },
			{ "min", {} },
			{ "max", {} },
			{ "pad", { "direction", "max", "value" } },
			{ "trim", { "direction" } },
			{ "substract", {} },
			{ "decrement", {} },
			{ "remainder", {} },
			{ "trunc", {} },
			{ "divide_trunc", {} },
			{ "divide_round", {} },
			{ "round", {} },
			{ "shift", {} },
			{ "replace", { "src", "dst", "all" } },
			{ "uppercase", {} },
			{ "lowercase", {} },
			{ "capitalize", {} },
			{ "case", { "src", "operator", "operator-format", "dst", "format", "default" } },
			{ "charset", { "id" } },
			{ "char", { "src", "dst", "default" } },
			{ "bitmask", { "id", "byte-completion" } },
			{ "character", { "mask" } },
			{ "sameas", { "id" } },
		};
		return allowed;
	}

	static const std::unordered_map<std::string, std::unordered_set<std::string>>& allowedChildrenByElement() {
		static const std::unordered_set<std::string> formatParts = {
			"add", "increment", "prefix", "multiply", "divide", "suffix", "postfix", "sum", "concat",
			"min", "max", "pad", "trim", "substract", "decrement", "remainder", "trunc",
			"divide_trunc", "divide_round", "round", "shift", "replace", "uppercase", "lowercase",
			"capitalize", "case"
		};
		static const std::unordered_set<std::string> columnRefs = { "field", "column" };
		static const std::unordered_set<std::string> concatChildren = { "field", "column", "txt" };
		static const std::unordered_map<std::string, std::unordered_set<std::string>> allowed = {
			{ "hi2txt", { "structure", "bitmask", "output", "format", "charset", "sameas" } },
			{ "structure", { "check", "elt", "loop" } },
			{ "check", { "definition", "size" } },
			{ "loop", { "elt" } },
			{ "output", { "table", "field" } },
			{ "table", { "column", "field" } },
			{ "format", formatParts },
			{ "add", columnRefs },
			{ "increment", columnRefs },
			{ "prefix", columnRefs },
			{ "multiply", columnRefs },
			{ "divide", columnRefs },
			{ "suffix", columnRefs },
			{ "postfix", columnRefs },
			{ "sum", columnRefs },
			{ "concat", concatChildren },
			{ "min", columnRefs },
			{ "max", columnRefs },
			{ "pad", columnRefs },
			{ "trim", columnRefs },
			{ "substract", columnRefs },
			{ "decrement", columnRefs },
			{ "remainder", columnRefs },
			{ "trunc", columnRefs },
			{ "divide_trunc", columnRefs },
			{ "divide_round", columnRefs },
			{ "round", columnRefs },
			{ "shift", columnRefs },
			{ "replace", columnRefs },
			{ "uppercase", columnRefs },
			{ "lowercase", columnRefs },
			{ "capitalize", columnRefs },
			{ "charset", { "char" } },
			{ "bitmask", { "character" } },
		};
		return allowed;
	}

	static std::string expectedChildrenText(const std::unordered_set<std::string>& children) {
		std::vector<std::string> ordered(children.begin(), children.end());
		std::sort(ordered.begin(), ordered.end());
		std::string out;
		for (size_t i = 0; i < ordered.size(); ++i) {
			if (i > 0) out += " ";
			out += ordered[i];
		}
		return out;
	}

	static bool validateDefinitionNode(rapidxml::xml_node<>* n, std::string& error) {
		if (!n || n->type() != rapidxml::node_element) return true;

		const std::string name = n->name() ? n->name() : "";
		const auto& attrsByElement = allowedAttrsByElement();
		const auto attrIt = attrsByElement.find(name);
		if (attrIt == attrsByElement.end()) {
			error = "The element '" + name + "' is not declared.";
			return false;
		}

		for (auto* a = n->first_attribute(); a; a = a->next_attribute()) {
			const std::string attrName = a->name() ? a->name() : "";
			if (attrIt->second.find(attrName) == attrIt->second.end()) {
				error = "The '" + attrName + "' attribute is not declared.";
				return false;
			}
		}

		const auto& childrenByElement = allowedChildrenByElement();
		const auto childIt = childrenByElement.find(name);
		for (auto* c = n->first_node(); c; c = c->next_sibling()) {
			if (c->type() != rapidxml::node_element) continue;
			const std::string childName = c->name() ? c->name() : "";

			if (attrsByElement.find(childName) == attrsByElement.end()) {
				error = "The element '" + childName + "' is not declared.";
				return false;
			}

			if (childIt == childrenByElement.end() || childIt->second.find(childName) == childIt->second.end()) {
				error = "The element '" + name + "' has invalid child element '" + childName + "'";
				if (childIt != childrenByElement.end() && !childIt->second.empty()) {
					error += ". List of possible elements expected: '" + expectedChildrenText(childIt->second) + "'.";
				}
				else {
					error += ".";
				}
				return false;
			}

			if (!validateDefinitionNode(c, error)) return false;
		}

		return true;
	}

	static std::vector<ConcatPart> parseMixedParts(rapidxml::xml_node<>* parent, bool allowTxtElement) {
		std::vector<ConcatPart> parts;
		if (!parent) return parts;
		for (auto* c = parent->first_node(); c; c = c->next_sibling()) {
			if (c->type() == rapidxml::node_data || c->type() == rapidxml::node_cdata) {
				std::string t = c->value() ? c->value() : "";
				if (!t.empty()) { ConcatPart p; p.kind = ConcatPartKind::Text; p.text = t; parts.push_back(std::move(p)); }
				continue;
			}
			const char* cn = c->name();
			if (!cn || *cn == '\0') continue;
			if (allowTxtElement && ieq(cn, "txt")) {
				ConcatPart p; p.kind = ConcatPartKind::Text; p.text = nodeTextRaw(c); parts.push_back(std::move(p));
				continue;
			}
			if (ieq(cn, "column") || ieq(cn, "field")) {
				ConcatPart p;
				p.id = attr(c, "src");
				if (p.id.empty()) p.id = attr(c, "id");
				p.format = formatAttr(c);
				p.kind = ConcatPartKind::Column;
				parts.push_back(std::move(p));
				continue;
			}
		}
		return parts;
	}

	// ----- <check><definition> parsing -----

	static bool isHexByteToken(const std::string& t) {
		if (t.empty()) return false;
		std::string s = t;
		if (s.size() >= 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s = s.substr(2);
		if (s.size() != 2) return false;
		return std::isxdigit((unsigned char)s[0]) && std::isxdigit((unsigned char)s[1]);
	}

	static std::vector<uint8_t> parseHexBytesFromTokens(const std::vector<std::string>& tokens) {
		std::vector<uint8_t> out;
		for (auto tok : tokens) {
			tok = trim(tok);
			if (tok.empty()) continue;

			if (isHexByteToken(tok)) {
				uint8_t by = (uint8_t)std::strtoul(tok.c_str(), nullptr, 16);
				out.push_back(by);
				continue;
			}
			if (tok.size() == 4 && (tok.rfind("0x", 0) == 0 || tok.rfind("0X", 0) == 0) && isHexByteToken(tok.substr(2))) {
				uint8_t by = (uint8_t)std::strtoul(tok.c_str() + 2, nullptr, 16);
				out.push_back(by);
				continue;
			}

			std::string s = tok;
			if (s.rfind("0x", 0) == 0 || s.rfind("0X", 0) == 0) s = s.substr(2);

			std::string hexOnly;
			for (char ch : s) if (std::isxdigit((unsigned char)ch)) hexOnly.push_back(ch);
			if (hexOnly.size() < 2) continue;
			if (hexOnly.size() % 2 == 1) hexOnly = "0" + hexOnly;
			for (size_t k = 0; k + 1 < hexOnly.size(); k += 2) {
				uint8_t by = (uint8_t)std::strtoul(hexOnly.substr(k, 2).c_str(), nullptr, 16);
				out.push_back(by);
			}
		}
		return out;
	}

	static bool parseLegacyDefinitionLine(const std::string& line, CheckDef& out) {
		std::string s = trim(line);
		if (s.empty()) return false;
		if (s.rfind("@:", 0) == 0) return false; // memory-space form; never file-signature

		// split by ':'
		std::vector<std::string> parts;
		{
			std::string cur;
			for (char ch : s) {
				if (ch == ':') { parts.push_back(cur); cur.clear(); }
				else cur.push_back(ch);
			}
			parts.push_back(cur);
		}
		if (parts.size() < 2) return false;

		// first part is offset
		out.offset = parseNum(parts[0]);
		if (out.offset < 0) out.offset = 0;

		// STRICT: remaining parts must each be a single byte token.
		std::vector<uint8_t> bytes;
		bytes.reserve(parts.size() - 1);

		for (size_t i = 1; i < parts.size(); ++i) {
			std::string tok = trim(parts[i]);
			if (tok.empty()) return false;

			// allow 0xNN or NN only
			if (!isHexByteToken(tok)) return false;

			// parse as byte
			if (tok.size() >= 2 && tok[0] == '0' && (tok[1] == 'x' || tok[1] == 'X'))
				tok = tok.substr(2);

			uint8_t by = (uint8_t)std::strtoul(tok.c_str(), nullptr, 16);
			bytes.push_back(by);
		}

		if (bytes.empty()) return false;
		out.bytes = std::move(bytes);
		return true;
	}

	static std::vector<CheckDef> parseLegacyDefinitionBlockAny(const std::string& payload) {
		std::vector<CheckDef> any;

		std::stringstream ss(payload);
		std::string line;
		while (std::getline(ss, line)) {
			CheckDef cd;
			if (parseLegacyDefinitionLine(line, cd)) any.push_back(std::move(cd));
		}

		if (any.empty()) {
			std::stringstream ss2(payload);
			std::string tok;
			while (ss2 >> tok) {
				CheckDef cd;
				if (parseLegacyDefinitionLine(tok, cd)) any.push_back(std::move(cd));
			}
		}

		return any;
	}

	static std::string normalizeXmlHiscoreDefinitionToken(const std::string& token) {
		std::vector<std::string> parts;
		std::string cur;
		for (char ch : trim(token)) {
			if (ch == ':') { parts.push_back(cur); cur.clear(); }
			else cur.push_back(ch);
		}
		parts.push_back(cur);

		if (parts.size() != 5) return "";
		if (!parts[0].empty() && parts[0][0] == '@') return "";

		std::string out;
		for (size_t i = 1; i < parts.size(); ++i) {
			if (i > 1) out += ":";
			std::string p = trim(parts[i]);
			if (p.size() >= 2 && p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) p = p.substr(2);
			if (p.empty()) return "";
			for (char& ch : p) ch = (char)std::tolower((unsigned char)ch);
			out += p;
		}
		return out;
	}

	static std::vector<std::string> parseHiscoreDefinitionTokens(const std::string& payload) {
		std::vector<std::string> out;
		std::stringstream ss(payload);
		std::string tok;
		while (ss >> tok) {
			std::string norm = normalizeXmlHiscoreDefinitionToken(tok);
			if (!norm.empty()) out.push_back(std::move(norm));
		}
		return out;
	}

	static bool parseHexIntToken(const std::string& raw, int& out) {
		std::string t = trim(raw);
		if (t.empty()) return false;
		if (t.size() >= 2 && t[0] == '0' && (t[1] == 'x' || t[1] == 'X')) t = t.substr(2);
		for (char ch : t) {
			if (!std::isxdigit((unsigned char)ch)) return false;
		}
		out = (int)std::strtol(t.c_str(), nullptr, 16);
		return true;
	}

	static bool parseHiscoreRangeToken(const std::string& token, int& length, uint8_t& first, uint8_t& last) {
		std::vector<std::string> parts;
		std::string cur;
		for (char ch : trim(token)) {
			if (ch == ':') { parts.push_back(cur); cur.clear(); }
			else cur.push_back(ch);
		}
		parts.push_back(cur);

		if (parts.size() != 5) return false;
		if (!parts[0].empty() && parts[0][0] == '@') return false;

		int addr = 0, len = 0, start = 0, end = 0;
		if (!parseHexIntToken(parts[1], addr)) return false;
		if (!parseHexIntToken(parts[2], len)) return false;
		if (!parseHexIntToken(parts[3], start)) return false;
		if (!parseHexIntToken(parts[4], end)) return false;
		if (len <= 0 || start < 0 || start > 255 || end < 0 || end > 255) return false;

		length = len;
		first = (uint8_t)start;
		last = (uint8_t)end;
		return true;
	}

	static std::vector<CheckDef> parseHiscoreRangeDefinitionBlockAll(const std::string& payload) {
		std::vector<CheckDef> all;
		size_t cursor = 0;

		std::stringstream ss(payload);
		std::string tok;
		while (ss >> tok) {
			int len = 0;
			uint8_t first = 0, last = 0;
			if (!parseHiscoreRangeToken(tok, len, first, last)) return {};

			// hi2txt treats the first saved range as the .hi signature; later
			// hiscore.dat ranges are init/check bytes, not file bytes to enforce.
			if (cursor > 0) {
				cursor += (size_t)len;
				continue;
			}

			if (first <= 1) {
				CheckDef firstCheck;
				firstCheck.offset = (int)cursor;
				firstCheck.bytes = { first };
				all.push_back(std::move(firstCheck));
			}

			if (len > 1) {
				CheckDef lastCheck;
				lastCheck.offset = (int)(cursor + (size_t)len - 1u);
				lastCheck.bytes = { last };
				all.push_back(std::move(lastCheck));
			}

			cursor += (size_t)len;
		}

		return all;
	}

	// ----- other helpers -----

	static void parseLineIgnoreRules(Table& tab) {
		tab.ignoreRules.clear();
		std::string raw = trim(tab.lineIgnoreRaw);
		if (raw.empty()) return;

		for (char& ch : raw) if (ch == ',') ch = ';';

		std::stringstream ss(raw);
		std::string tok;
		while (std::getline(ss, tok, ';')) {
			tok = trim(tok);
			if (tok.empty()) continue;
			size_t c = tok.find(':');
			if (c == std::string::npos) continue;
			IgnoreRule r;
			r.colId = trim(tok.substr(0, c));
			r.value = trim(tok.substr(c + 1));
			if (!r.colId.empty()) tab.ignoreRules.push_back(std::move(r));
		}
	}

	static IntBaseKind parseIntBaseKind(const std::string& baseStrRaw) {
		std::string b = trim(baseStrRaw);
		if (b.empty()) return IntBaseKind::Decimal;
		if (ieq(b, "16")) return IntBaseKind::BcdBE;
		if (ieq(b, "hex") || ieq(b, "hexa")) return IntBaseKind::Hex;
		if (ieq(b, "bcd") || ieq(b, "bcd-be")) return IntBaseKind::BcdBE;
		if (ieq(b, "bcd-le")) return IntBaseKind::BcdLE;
		int n = parseNum(b);
		if (n == 16) return IntBaseKind::BcdBE;
		return IntBaseKind::Decimal;
	}

	static int parseCharsetSrc(const std::string& raw) {
		std::string s = trim(raw);
		if (s.empty()) return -1;
		if (s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
			return (int)std::strtol(s.c_str() + 2, nullptr, 16);

		if (s.size() == 2) {
			bool allHex = true;
			for (char ch : s) {
				if (!std::isxdigit((unsigned char)ch)) { allHex = false; break; }
			}
			if (allHex) return (int)std::strtol(s.c_str(), nullptr, 16);
		}

		return parseNum(s);
	}

	static std::vector<uint8_t> parseBitStringToMaskBytes(const std::string& maskAttr) {
		std::vector<uint8_t> out;
		std::stringstream ss(maskAttr);
		std::string tok;
		while (ss >> tok) {
			if (tok.size() != 8) continue;
			uint8_t b = 0;
			for (size_t i = 0; i < 8; ++i) {
				b <<= 1;
				if (tok[i] == '1') b |= 1;
			}
			out.push_back(b);
		}
		return out;
	}

	std::string XmlParser::getSameAsId(const std::string& xmlText) {
		using namespace rapidxml;

		std::string buf = xmlText;
		buf.push_back('\0');

		xml_document<> doc;
		try { doc.parse<parse_non_destructive>(&buf[0]); }
		catch (...) { return ""; }

		auto* root = doc.first_node("hi2txt");
		if (!root) return "";

		// accept both <sameas id="pacman"/> and <sameas>pacman</sameas>
		if (auto* sa = root->first_node("sameas")) {
			if (auto* a = sa->first_attribute("id")) {
				return Utils::trim(std::string(a->value(), a->value_size()));
			}
			if (sa->value() && sa->value_size() > 0) {
				return Utils::trim(std::string(sa->value(), sa->value_size()));
			}
		}
		return "";
	}

	static GameDef parseUnchecked(const std::string& xml) {
		GameDef def;

		rapidxml::xml_document<> doc;
		std::vector<char> buf(xml.begin(), xml.end());
		buf.push_back('\0');

		try { doc.parse<0>(buf.data()); }
		catch (...) { return def; }

		auto* root = doc.first_node("hi2txt");
		if (!root) root = doc.first_node();
		if (!root) return def;

		for (auto* n = root->first_node(); n; n = n->next_sibling()) {
			const char* nm = n->name();
			if (!nm || *nm == '\0') continue;

			if (ieq(nm, "format")) {
				FormatDef f;
				f.id = attr(n, "id");
				f.formatter = attr(n, "formatter");

				{
					std::string ap = attr(n, "apply-to");
					if (ieq(ap, "char")) f.applyTo = ApplyToKind::Char;
					else f.applyTo = ApplyToKind::Value;
				}
				{
					std::string sub = attr(n, "input-as-subcolumns-input");
					if (!sub.empty()) f.inputAsSubcolumnsInput = Utils::parseBool(sub, false);
				}

				for (auto* c = n->first_node(); c; c = c->next_sibling()) {
					const char* cn = c->name();
					if (!cn || *cn == '\0') continue;

					if (ieq(cn, "add") || ieq(cn, "increment")) f.mathOps.push_back({ FormatKind::Add, std::atof(nodeText(c).c_str()) });
					else if (ieq(cn, "substract") || ieq(cn, "decrement")) f.mathOps.push_back({ FormatKind::Substract, std::atof(nodeText(c).c_str()) });
					else if (ieq(cn, "multiply")) f.mathOps.push_back({ FormatKind::Multiply, std::atof(nodeText(c).c_str()) });
					else if (ieq(cn, "divide")) f.mathOps.push_back({ FormatKind::Divide, std::atof(nodeText(c).c_str()) });
					else if (ieq(cn, "remainder")) f.mathOps.push_back({ FormatKind::Remainder, std::atof(nodeText(c).c_str()) });
					else if (ieq(cn, "divide_trunc")) f.mathOps.push_back({ FormatKind::DivideTrunc, std::atof(nodeText(c).c_str()) });
					else if (ieq(cn, "divide_round")) f.mathOps.push_back({ FormatKind::DivideRound, std::atof(nodeText(c).c_str()) });
					else if (ieq(cn, "shift")) f.mathOps.push_back({ FormatKind::Shift, std::atof(nodeText(c).c_str()) });

					else if (ieq(cn, "round")) f.doRound = true;
					else if (ieq(cn, "trunc")) f.doTrunc = true;
					else if (ieq(cn, "loopindex")) f.doLoopIndex = true;

					else if (ieq(cn, "prefix")) {
						AffixOp a;
						a.emptyValue = attrRaw(c, "empty");
						a.hasEmpty = c->first_attribute("empty") != nullptr;
						a.consume = Utils::parseBool(attr(c, "consume"), false);
						a.parts = parseMixedParts(c, false);
						if (a.parts.empty()) { ConcatPart p; p.kind = ConcatPartKind::Text; p.text = nodeTextRaw(c); a.parts.push_back(std::move(p)); }
						f.prefixes.push_back(std::move(a));
					}
					else if (ieq(cn, "suffix") || ieq(cn, "postfix")) {
						AffixOp a;
						a.emptyValue = attrRaw(c, "empty");
						a.hasEmpty = c->first_attribute("empty") != nullptr;
						a.consume = Utils::parseBool(attr(c, "consume"), false);
						a.parts = parseMixedParts(c, false);
						if (a.parts.empty()) { ConcatPart p; p.kind = ConcatPartKind::Text; p.text = nodeTextRaw(c); a.parts.push_back(std::move(p)); }
						f.suffixes.push_back(std::move(a));
					}
					else if (ieq(cn, "lowercase")) f.lowercase = true;
					else if (ieq(cn, "uppercase")) f.uppercase = true;
					else if (ieq(cn, "capitalize")) f.capitalize = true;
					else if (ieq(cn, "replace")) {
						ReplaceOp rop;
						rop.enabled = true;
						rop.src = attrRaw(c, "src");
						rop.dst = attrRaw(c, "dst");
						std::string allv = attr(c, "all");
						if (!allv.empty()) rop.all = Utils::parseBool(allv, true);
						if (rop.src.empty() && rop.dst.empty()) {
							std::string t = nodeText(c);
							size_t p = t.find(':');
							if (p != std::string::npos) { rop.src = t.substr(0, p); rop.dst = t.substr(p + 1); }
						}
						f.repl = rop;
						f.repls.push_back(std::move(rop));
					}
					else if (ieq(cn, "pad")) {
						f.pad.enabled = true;
						std::string dir = attr(c, "direction");
						f.pad.dir = ieq(dir, "right") ? TrimDir::Right : TrimDir::Left;
						f.pad.max = std::atoi(attr(c, "max").c_str());
						f.pad.padChar = attrRaw(c, "value");
						if (f.pad.padChar.empty()) f.pad.padChar = nodeTextRaw(c);
						if (f.pad.padChar.empty()) f.pad.padChar = "0";
					}
					else if (ieq(cn, "case")) {
						CaseMap cm;
						cm.src = attrRaw(c, "src");

						cm.hasDst = (c->first_attribute("dst") != nullptr);
						cm.dst = attrRaw(c, "dst");                          // may be ""

						cm.isDefault = Utils::parseBool(attr(c, "default"), false);
						cm.op = attrRaw(c, "operator");
						cm.operatorFormat = formatAttr(c, "operator-format");
						cm.format = formatAttr(c);
						f.cases.push_back(std::move(cm));
					}
					else if (ieq(cn, "trim")) {
						TrimOp op;
						std::string dir = attr(c, "direction");
						if (ieq(dir, "right")) op.dir = TrimDir::Right;
						else if (ieq(dir, "left")) op.dir = TrimDir::Left;
						else if (ieq(dir, "both")) op.dir = TrimDir::Both;
						else op.dir = TrimDir::Right;
						op.chars = nodeTextRaw(c);
						if (op.chars.empty()) op.chars = " ";
						f.trim = op;
						f.trims.push_back(std::move(op));
					}
					else if (ieq(cn, "sum")) {
						for (auto* cc = c->first_node(); cc; cc = cc->next_sibling()) {
							const char* ccn = cc->name();
							if (!ccn || *ccn == '\0') continue;
							if (ieq(ccn, "column") || ieq(ccn, "field")) {
								FormatColRef r;
								r.id = attr(cc, "src");
								if (r.id.empty()) r.id = attr(cc, "id");
								r.format = formatAttr(cc);
								if (!r.id.empty()) f.sumCols.push_back(std::move(r));
							}
						}
					}
					else if (ieq(cn, "concat")) {
						for (auto* cc = c->first_node(); cc; cc = cc->next_sibling()) {
							const char* ccn = cc->name();
							if (!ccn || *ccn == '\0') continue;

							if (ieq(ccn, "txt")) {
								ConcatPart p;
								p.kind = ConcatPartKind::Text;
								p.text = nodeTextRaw(cc);
								f.concatParts.push_back(std::move(p));
								continue;
							}

							if (ieq(ccn, "column") || ieq(ccn, "field")) {
								ConcatPart p;
								p.format = formatAttr(cc);
								p.id = attr(cc, "src");
								if (p.id.empty()) p.id = attr(cc, "id");

								// NEW: if node has inline text, it's a literal fragment (hi2txt behavior),
								// regardless of whether id is present.
								std::string lit = nodeTextRaw(cc);
								if (!Utils::trim(lit).empty()) {
									p.kind = ConcatPartKind::Text;
									p.text = lit;
									f.concatParts.push_back(std::move(p));
									continue;
								}

								// Normal column/field reference by id
								if (!p.id.empty()) {
									p.kind = ConcatPartKind::Column;
									f.concatParts.push_back(std::move(p));
									continue;
								}

								// Apply format to current input (subcolumns) when enabled
								p.kind = ConcatPartKind::Input;
								f.concatParts.push_back(std::move(p));
								continue;

								// else: ignore
							}
						}
					}

					else if (ieq(cn, "min")) {
						for (auto* cc = c->first_node(); cc; cc = cc->next_sibling()) {
							const char* ccn = cc->name();
							if (!ccn || *ccn == '\0') continue;
							if (ieq(ccn, "column") || ieq(ccn, "field")) {
								FormatColRef r;
								r.id = attr(cc, "src");
								if (r.id.empty()) r.id = attr(cc, "id");
								r.format = formatAttr(cc);
								if (!r.id.empty()) f.minCols.push_back(std::move(r));
							}
						}
					}
					else if (ieq(cn, "max")) {
						for (auto* cc = c->first_node(); cc; cc = cc->next_sibling()) {
							const char* ccn = cc->name();
							if (!ccn || *ccn == '\0') continue;
							if (ieq(ccn, "column") || ieq(ccn, "field")) {
								FormatColRef r;
								r.id = attr(cc, "src");
								if (r.id.empty()) r.id = attr(cc, "id");
								r.format = formatAttr(cc);
								if (!r.id.empty()) f.maxCols.push_back(std::move(r));
							}
						}
					}

				}

				if (!f.id.empty()) def.formats[f.id] = std::move(f);
			}
			else if (ieq(nm, "charset")) {
				Charset cs;
				cs.id = attr(n, "id");
				for (auto* c = n->first_node("char"); c; c = c->next_sibling("char")) {
					int b = parseCharsetSrc(attr(c, "src"));
					if (b >= 0) {
						std::string dst = attrRaw(c, "dst");
						cs.entries[(uint32_t)b] = dst;
						if (b <= 255) {
							cs.dst[b] = dst;
							cs.used[b] = true;
						}
					}
				}
				if (!cs.id.empty()) def.charsets[cs.id] = std::move(cs);
			}
			else if (ieq(nm, "bitmask")) {
				BitmaskDef bm;
				bm.id = attr(n, "id");
				bm.byteCompletion = Utils::parseBool(attr(n, "byte-completion"), true);

				std::vector<uint8_t> merged;

				for (auto* c = n->first_node("character"); c; c = c->next_sibling("character")) {
					std::string m = attrRaw(c, "mask");
					auto bytes = parseBitStringToMaskBytes(m);
					if (bytes.empty()) continue;

					// preserve per-character masks (order matters)
					bm.charMasks.push_back(bytes);

					// also build merged OR mask (old behavior)
					if (merged.empty()) merged = bytes;
					else {
						if (merged.size() < bytes.size()) merged.resize(bytes.size(), 0);
						for (size_t i = 0; i < bytes.size(); ++i) merged[i] |= bytes[i];
					}
				}

				bm.mergedMask = std::move(merged);

				if (!bm.id.empty() && (!bm.charMasks.empty() || !bm.mergedMask.empty()))
					def.bitmasks[bm.id] = std::move(bm);
			}
			else if (ieq(nm, "structure")) {
				Structure s;
				s.fileKind = attr(n, "file");
				s.byteSwap = std::atoi(attr(n, "byte-swap").c_str());
				s.outputId = attr(n, "output");

				if (auto* ck = n->first_node("check")) {

					// --- sizes are OR, not single ---
					for (auto* sz = ck->first_node("size"); sz; sz = sz->next_sibling("size")) {
						std::string t = nodeText(sz);
						int v = std::atoi(t.c_str());
						if (v > 0) s.checkSizes.push_back(v);
					}

					// --- definitions (keep your existing parsing for now) ---
					for (auto* d = ck->first_node("definition"); d; d = d->next_sibling("definition")) {
						std::string offAttr = attr(d, "offset");
						std::string payload = nodeText(d);
						auto defTokens = parseHiscoreDefinitionTokens(payload);
						const bool hasHiscoreDefinitionTokens = !defTokens.empty();
						for (auto& tok : defTokens) s.hiscoreDefinitionTokens.push_back(std::move(tok));

						if (!offAttr.empty()) {
							CheckDef cd;
							cd.offset = parseNum(offAttr);
							if (cd.offset < 0) cd.offset = 0;

							std::vector<std::string> toks;
							std::stringstream ss(payload);
							std::string tok;
							while (ss >> tok) toks.push_back(tok);
							cd.bytes = parseHexBytesFromTokens(toks);

							if (!cd.bytes.empty()) s.checkAll.push_back(std::move(cd));
							continue;
						}

						if (hasHiscoreDefinitionTokens) {
							continue;
						}

						auto rangeChecks = parseHiscoreRangeDefinitionBlockAll(payload);
						if (!rangeChecks.empty()) {
							for (auto& cd : rangeChecks) s.checkAll.push_back(std::move(cd));
							continue;
						}

						auto any = parseLegacyDefinitionBlockAny(payload);
						for (auto& cd : any) s.checkAny.push_back(std::move(cd));
					}
				}
				auto parseE = [&](rapidxml::xml_node<>* e) {
					Elt el;

					el.id = attr(e, "id");
					el.type = attr(e, "type");
					el.size = std::atoi(attr(e, "size").c_str());
					el.offset = parseNum(attr(e, "offset"));
					el.format = formatAttr(e);

					// keep raw decoding-profile; semantics handled in Processor (decodeInt/decodeText)
					el.decodingProfile = attr(e, "decoding-profile");

					// endianness
					{
						std::string end = attr(e, "endianness");
						if (ieq(end, "little_endian")) el.endian = Endianness::Little;
					}

					// byte ops
					el.byteSkip = attr(e, "byte-skip");
					el.byteTrim = attr(e, "byte-trim");
					el.byteTrunc = attr(e, "byte-trunc");
					el.byteSwap = std::atoi(attr(e, "byte-swap").c_str());

					// nibble ops
					el.nibbleSkip = attr(e, "nibble-skip");
					el.nibbleTrim = attr(e, "nibble-trim");

					// ---- decoding-profile shortcut expansion (spec) ----
					// Apply only when explicit attributes are absent so explicit settings win.
					// NOTE: Do NOT set el.intBase for dp=bcd/bcd-le here; scaling semantics live in decodeInt().
					{
						const std::string dp = el.decodingProfile;
						if (!dp.empty()) {
							const bool hasEndianness = (e->first_attribute("endianness") != nullptr);
							const bool hasNibbleSkip = (e->first_attribute("nibble-skip") != nullptr);
							const bool hasBase = (e->first_attribute("base") != nullptr);
							const bool hasSrcUnit = (e->first_attribute("src-unit-size") != nullptr);
							const bool hasDstUnit = (e->first_attribute("dst-unit-size") != nullptr);
							const bool hasAsciiOff = (e->first_attribute("ascii-offset") != nullptr);

							if (ieq(dp, "base-40") || ieq(dp, "base-32")) {
								// packed TEXT: 16-bit unit -> 3 digits in base32/base40, then ascii-offset 64
								if (!hasSrcUnit) el.srcUnitSizeBits = 16;
								if (!hasDstUnit) el.dstUnitCount = 3;
								if (!hasAsciiOff) { el.asciiOffset = 64; el.hasAsciiOffset = true; }

								// Only for TEXT path; leave intBase alone.
								if (!hasBase) {
									el.textBase = ieq(dp, "base-40") ? 40 : 32;
									el.hasTextBase = true;
								}
							}
							else if (ieq(dp, "bcd")) {
								// supply missing nibble-skip hint only
								if (!hasNibbleSkip) el.nibbleSkip = "odd";
							}
							else if (ieq(dp, "bcd-le")) {
								// supply missing LE + nibble-skip hints only
								if (!hasEndianness) el.endian = Endianness::Little;
								if (!hasNibbleSkip) el.nibbleSkip = "odd";
							}
						}
					}
					// ----------------------------------------------------

					el.swapSkipOrder = attr(e, "swap-skip-order");

					// bit ops
					{
						std::string bs = attr(e, "bit-swap");
						if (!bs.empty()) el.bitSwap = ieq(bs, "yes") || ieq(bs, "1") || ieq(bs, "true");
					}
					el.bitmaskId = attr(e, "bitmask");

					// base=".." is used for:
					// - int: hex / bcd-be / bcd-le / 16 -> BCD
					// - text: numeric base for packed base-32/base-40 decoding
					{
						std::string baseAttr = attr(e, "base");
						if (ieq(el.type, "text")) {
							int bn = parseNum(baseAttr);
							if (bn == 32 || bn == 40) {
								el.textBase = bn;
								el.hasTextBase = true;
							}
						}
						else {
							// explicit base wins (including base=16 => BCD). decoding-profile may still apply scaling later.
							el.intBase = parseIntBaseKind(baseAttr);
							int bn = parseNum(baseAttr);
							if (bn > 1 && bn != 10 && bn != 16) {
								el.numericBase = bn;
							}
						}
					}

					// ascii controls
					if (auto* a = e->first_attribute("ascii-offset")) {
						el.asciiOffset = std::atoi(a->value());
						el.hasAsciiOffset = true;
					}
					if (auto* a = e->first_attribute("ascii-step")) {
						el.asciiStep = std::atoi(a->value());
						if (el.asciiStep == 0) el.asciiStep = 1;
						el.hasAsciiStep = true;
					}

					el.charsetId = attr(e, "charset");

					// IMPORTANT: only assign if the attribute exists; otherwise preserve decoding-profile defaults.
					if (auto* a = e->first_attribute("src-unit-size")) {
						el.srcUnitSizeBits = std::atoi(a->value());
					}
					if (auto* a = e->first_attribute("dst-unit-size")) {
						el.dstUnitCount = std::atoi(a->value());
					}

					// table-index
					{
						std::string ti = attr(e, "table-index");
						if (!ti.empty()) {
							if (ieq(ti, "loop_index")) el.tableIndexKind = TableIndexKind::LoopIndex;
							else if (ieq(ti, "loop_reverse_index")) el.tableIndexKind = TableIndexKind::LoopReverseIndex;
							else if (ieq(ti, "last")) el.tableIndexKind = TableIndexKind::Last;
							else if (ieq(ti, "itself")) el.tableIndexKind = TableIndexKind::Itself;
							else {
								size_t p = ti.find(':');
								if (p != std::string::npos) {
									el.tableIndexCol = trim(ti.substr(0, p));
									std::string rhs = trim(ti.substr(p + 1));
									if (ieq(rhs, "index_from_value")) el.tableIndexKind = TableIndexKind::ValueFromIndex;
									else if (ieq(rhs, "value_from_index")) el.tableIndexKind = TableIndexKind::IndexFromValue;
									else {
										int v = parseNum(ti);
										if (v >= 0) { el.tableIndexKind = TableIndexKind::Fixed; el.tableIndexFixed = v; }
									}
								}
								else {
									int v = parseNum(ti);
									if (v >= 0) { el.tableIndexKind = TableIndexKind::Fixed; el.tableIndexFixed = v; }
								}
							}
						}
					}

					el.tableIndexFormat = attrRaw(e, "table-index-format");
					return el;
					};

				for (auto* c = n->first_node(); c; c = c->next_sibling()) {
					const char* cn = c->name();
					if (!cn || *cn == '\0') continue;

					if (ieq(cn, "elt")) {
						StructureItem it;
						it.kind = StructureItem::Kind::Elt;
						it.elt = parseE(c);
						s.items.push_back(std::move(it));
					}
					else if (ieq(cn, "loop")) {
						StructureItem it;
						it.kind = StructureItem::Kind::Loop;

						it.loop.count = std::atoi(attr(c, "count").c_str());

						if (c->first_attribute("start")) {
							it.loop.startIndex = std::atoi(attr(c, "start").c_str());
							it.loop.hasStart = true;
						}
						if (c->first_attribute("step")) {
							it.loop.step = std::atoi(attr(c, "step").c_str());
							if (it.loop.step == 0) it.loop.step = 1;
							it.loop.hasStep = true;
						}
						if (c->first_attribute("skip-first-bytes")) {
							it.loop.skipFirstBytes = std::atoi(attr(c, "skip-first-bytes").c_str());
							if (it.loop.skipFirstBytes < 0) it.loop.skipFirstBytes = 0;
						}
						if (c->first_attribute("skip-last-bytes")) {
							it.loop.skipLastBytes = std::atoi(attr(c, "skip-last-bytes").c_str());
							if (it.loop.skipLastBytes < 0) it.loop.skipLastBytes = 0;
						}

						for (auto* ee = c->first_node("elt"); ee; ee = ee->next_sibling("elt"))
							it.loop.elts.push_back(parseE(ee));

						s.items.push_back(std::move(it));
					}
				}

				def.structures.push_back(std::move(s));
			}
			else if (ieq(nm, "output")) {
				const std::string outId = attr(n, "id");               // "" means default output
				const bool firstOutputWithId = def.outputs.find(outId) == def.outputs.end();
				OutputDef& out = def.outputs[outId];
				if (firstOutputWithId) def.outputOrder.push_back(outId);
				out.id = outId;

				// --------------------
				// Tables
				// --------------------
				for (auto* t = n->first_node("table"); t; t = t->next_sibling("table")) {
					Table tab;
					tab.id = attr(t, "id");
					tab.display = attr(t, "display");
					tab.lineIgnoreRaw = attr(t, "line-ignore");
					tab.sortKey = attr(t, "sort");
					tab.sortOrder = attr(t, "sort-order");
					tab.sortFormat = formatAttr(t, "sort-format");
					tab.linesMax = std::max(0, std::atoi(attr(t, "lines-max").c_str()));

					std::string igop = attr(t, "line-ignore-operator");
					if (ieq(igop, "or")) tab.ignoreOp = IgnoreOp::Or;
					else if (ieq(igop, "and") || igop.empty()) tab.ignoreOp = IgnoreOp::And;
					else tab.ignoreCompareOp = igop;

					parseLineIgnoreRules(tab);

					for (auto* c = t->first_node(); c; c = c->next_sibling()) {
						const char* cn = c->name();
						if (!cn || *cn == '\0') continue;

						if (ieq(cn, "column") || ieq(cn, "field")) {
							Column col{ attr(c, "id"), attr(c, "src"), formatAttr(c), attr(c, "display") };

							if (trim(col.src).empty()) col.src = col.id;
							if (col.id.empty() && !col.src.empty()) col.id = col.src;

							if (!col.id.empty())
								tab.cols.push_back(std::move(col));
						}
					}

					if (!tab.cols.empty())
						out.tables.push_back(std::move(tab));
				}

				// --------------------
				// Output-level fields (direct children of <output>)
				// --------------------
				for (auto* f = n->first_node("field"); f; f = f->next_sibling("field")) {
					Column fld{ attr(f, "id"), attr(f, "src"), formatAttr(f), attr(f, "display") };

					if (trim(fld.src).empty()) fld.src = fld.id;
					if (fld.id.empty() && !fld.src.empty()) fld.id = fld.src;

					if (!fld.id.empty())
						out.fields.push_back(std::move(fld));
				}
			}
		}

		return def;
	}

	XmlParseResult XmlParser::parseWithDiagnostics(const std::string& xml) {
		XmlParseResult res;

		rapidxml::xml_document<> doc;
		std::vector<char> buf(xml.begin(), xml.end());
		buf.push_back('\0');

		try {
			doc.parse<0>(buf.data());
		}
		catch (const rapidxml::parse_error& e) {
			res.error = parseErrorWithLocation(e.what(), buf.data(), e.where<char>());
			return res;
		}
		catch (const std::exception& e) {
			res.error = e.what();
			return res;
		}
		catch (...) {
			res.error = "Unknown XML parse error.";
			return res;
		}

		auto* root = doc.first_node("hi2txt");
		if (!root) root = doc.first_node();
		if (!root) {
			res.error = "No root element found.";
			return res;
		}

		std::string schemaError;
		if (!validateDefinitionNode(root, schemaError)) {
			res.error = schemaError;
			return res;
		}

		res.def = parseUnchecked(xml);
		res.ok = true;
		return res;
	}

	GameDef XmlParser::parse(const std::string& xml) {
		return parseWithDiagnostics(xml).def;
	}

} // namespace openhi2txt
