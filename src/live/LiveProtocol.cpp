#include "LiveProtocol.h"

#include <algorithm>
#include <cctype>
#include <limits>
#include <sstream>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace openhi2txt::live_detail {
namespace {

constexpr std::size_t kMaximumSnapshotBytes = 32U * 1024U * 1024U;

struct JsonValue {
    enum class Kind { Null, Boolean, Number, String, Array, Object } kind = Kind::Null;
    bool boolean = false;
    std::uint64_t number = 0;
    std::string string;
    std::vector<JsonValue> array;
    std::unordered_map<std::string, JsonValue> object;
};

void appendUtf8(std::string& output, unsigned codepoint) {
    if (codepoint <= 0x7f) output.push_back(static_cast<char>(codepoint));
    else if (codepoint <= 0x7ff) {
        output.push_back(static_cast<char>(0xc0 | (codepoint >> 6)));
        output.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
    }
    else {
        output.push_back(static_cast<char>(0xe0 | (codepoint >> 12)));
        output.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
        output.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
    }
}

class JsonParser {
public:
    explicit JsonParser(std::string_view input) : input_(input) {}

    bool parse(JsonValue& value, std::string& error) {
        skipSpace();
        if (!parseValue(value, error)) return false;
        skipSpace();
        if (position_ != input_.size()) {
            error = "unexpected data after JSON value";
            return false;
        }
        return true;
    }

private:
    void skipSpace() {
        while (position_ < input_.size() &&
               std::isspace(static_cast<unsigned char>(input_[position_]))) ++position_;
    }

    bool consume(char wanted) {
        skipSpace();
        if (position_ >= input_.size() || input_[position_] != wanted) return false;
        ++position_;
        return true;
    }

    bool parseValue(JsonValue& value, std::string& error) {
        skipSpace();
        if (position_ >= input_.size()) {
            error = "unexpected end of JSON";
            return false;
        }
        const char current = input_[position_];
        if (current == '{') return parseObject(value, error);
        if (current == '[') return parseArray(value, error);
        if (current == '"') {
            value.kind = JsonValue::Kind::String;
            return parseString(value.string, error);
        }
        if (current >= '0' && current <= '9') return parseNumber(value, error);
        if (input_.substr(position_, 4) == "null") {
            position_ += 4;
            value.kind = JsonValue::Kind::Null;
            return true;
        }
        if (input_.substr(position_, 4) == "true") {
            position_ += 4;
            value.kind = JsonValue::Kind::Boolean;
            value.boolean = true;
            return true;
        }
        if (input_.substr(position_, 5) == "false") {
            position_ += 5;
            value.kind = JsonValue::Kind::Boolean;
            value.boolean = false;
            return true;
        }
        error = "unsupported JSON value";
        return false;
    }

    bool parseObject(JsonValue& value, std::string& error) {
        ++position_;
        value.kind = JsonValue::Kind::Object;
        skipSpace();
        if (consume('}')) return true;
        while (true) {
            std::string key;
            if (!parseString(key, error)) return false;
            if (!consume(':')) {
                error = "expected ':' in JSON object";
                return false;
            }
            JsonValue child;
            if (!parseValue(child, error)) return false;
            value.object[std::move(key)] = std::move(child);
            if (consume('}')) return true;
            if (!consume(',')) {
                error = "expected ',' in JSON object";
                return false;
            }
        }
    }

    bool parseArray(JsonValue& value, std::string& error) {
        ++position_;
        value.kind = JsonValue::Kind::Array;
        skipSpace();
        if (consume(']')) return true;
        while (true) {
            JsonValue child;
            if (!parseValue(child, error)) return false;
            value.array.push_back(std::move(child));
            if (consume(']')) return true;
            if (!consume(',')) {
                error = "expected ',' in JSON array";
                return false;
            }
        }
    }

public:
    static int hexDigit(char value) {
        if (value >= '0' && value <= '9') return value - '0';
        if (value >= 'a' && value <= 'f') return value - 'a' + 10;
        if (value >= 'A' && value <= 'F') return value - 'A' + 10;
        return -1;
    }

private:

    bool parseString(std::string& output, std::string& error) {
        skipSpace();
        if (position_ >= input_.size() || input_[position_] != '"') {
            error = "expected JSON string";
            return false;
        }
        ++position_;
        while (position_ < input_.size()) {
            const unsigned char current = static_cast<unsigned char>(input_[position_++]);
            if (current == '"') return true;
            if (current < 0x20) {
                error = "control character in JSON string";
                return false;
            }
            if (current != '\\') {
                output.push_back(static_cast<char>(current));
                continue;
            }
            if (position_ >= input_.size()) break;
            const char escaped = input_[position_++];
            switch (escaped) {
            case '"': output.push_back('"'); break;
            case '\\': output.push_back('\\'); break;
            case '/': output.push_back('/'); break;
            case 'b': output.push_back('\b'); break;
            case 'f': output.push_back('\f'); break;
            case 'n': output.push_back('\n'); break;
            case 'r': output.push_back('\r'); break;
            case 't': output.push_back('\t'); break;
            case 'u': {
                if (position_ + 4 > input_.size()) {
                    error = "short Unicode escape in JSON string";
                    return false;
                }
                unsigned codepoint = 0;
                for (int i = 0; i < 4; ++i) {
                    const int digit = hexDigit(input_[position_++]);
                    if (digit < 0) {
                        error = "invalid Unicode escape in JSON string";
                        return false;
                    }
                    codepoint = (codepoint << 4) | static_cast<unsigned>(digit);
                }
                if (codepoint >= 0xd800 && codepoint <= 0xdfff) {
                    error = "JSON surrogate escapes are not supported";
                    return false;
                }
                appendUtf8(output, codepoint);
                break;
            }
            default:
                error = "invalid JSON string escape";
                return false;
            }
        }
        error = "unterminated JSON string";
        return false;
    }

    bool parseNumber(JsonValue& value, std::string& error) {
        const std::size_t begin = position_;
        while (position_ < input_.size() && input_[position_] >= '0' && input_[position_] <= '9')
            ++position_;
        if (position_ - begin > 1 && input_[begin] == '0') {
            error = "invalid JSON number";
            return false;
        }
        std::uint64_t number = 0;
        for (std::size_t index = begin; index < position_; ++index) {
            const unsigned digit = static_cast<unsigned>(input_[index] - '0');
            if (number > ((std::numeric_limits<std::uint64_t>::max)() - digit) / 10) {
                error = "JSON number is too large";
                return false;
            }
            number = number * 10 + digit;
        }
        value.kind = JsonValue::Kind::Number;
        value.number = number;
        return true;
    }

    std::string_view input_;
    std::size_t position_ = 0;
};

const JsonValue* member(const JsonValue& object, const char* name) {
    if (object.kind != JsonValue::Kind::Object) return nullptr;
    const auto found = object.object.find(name);
    return found == object.object.end() ? nullptr : &found->second;
}

bool readString(const JsonValue& object, const char* name, std::string& output) {
    const JsonValue* value = member(object, name);
    if (!value || value->kind != JsonValue::Kind::String) return false;
    output = value->string;
    return true;
}

bool readNumber(const JsonValue& object, const char* name, std::uint64_t& output) {
    const JsonValue* value = member(object, name);
    if (!value || value->kind != JsonValue::Kind::Number) return false;
    output = value->number;
    return true;
}

bool decodeHex(std::string_view encoded, std::vector<std::uint8_t>& decoded) {
    if ((encoded.size() & 1U) != 0 || encoded.size() / 2U > kMaximumSnapshotBytes) return false;
    decoded.resize(encoded.size() / 2U);
    for (std::size_t index = 0; index < decoded.size(); ++index) {
        const int high = JsonParser::hexDigit(encoded[index * 2]);
        const int low = JsonParser::hexDigit(encoded[index * 2 + 1]);
        if (high < 0 || low < 0) return false;
        decoded[index] = static_cast<std::uint8_t>((high << 4) | low);
    }
    return true;
}

std::string quote(std::string_view value) {
    std::string output;
    output.reserve(value.size() + 2);
    output.push_back('"');
    static const char hex[] = "0123456789abcdef";
    for (const unsigned char current : value) {
        switch (current) {
        case '"': output += "\\\""; break;
        case '\\': output += "\\\\"; break;
        case '\b': output += "\\b"; break;
        case '\f': output += "\\f"; break;
        case '\n': output += "\\n"; break;
        case '\r': output += "\\r"; break;
        case '\t': output += "\\t"; break;
        default:
            if (current < 0x20) {
                output += "\\u00";
                output.push_back(hex[current >> 4]);
                output.push_back(hex[current & 0x0f]);
            }
            else output.push_back(static_cast<char>(current));
        }
    }
    output.push_back('"');
    return output;
}

bool sourceExpected(const std::string& source,
                    const std::string& expectedSource,
                    const std::vector<WatchRequest>& requests) {
    if (!expectedSource.empty()) return source == expectedSource;
    return std::any_of(requests.begin(), requests.end(), [&](const auto& request) {
        return request.source == source;
    });
}

} // namespace

bool parseEnvelope(const std::string& line, Envelope& envelope, std::string& error) {
    JsonValue root;
    if (!JsonParser(line).parse(root, error) || root.kind != JsonValue::Kind::Object) {
        if (error.empty()) error = "live message is not a JSON object";
        return false;
    }
    std::string protocol;
    std::uint64_t protocolVersion = 0;
    if (!readString(root, "protocol", protocol) || protocol != "openhi2txt-live" ||
        !readNumber(root, "protocolVersion", protocolVersion) || protocolVersion != 1 ||
        !readString(root, "type", envelope.type) ||
        (envelope.type != "hello" && envelope.type != "snapshot")) {
        error = "unsupported live protocol message";
        return false;
    }
    const JsonValue* identity = member(root, "identity");
    if (!identity || !readString(*identity, "machine", envelope.identity.machine) ||
        !readString(*identity, "softwareList", envelope.identity.softwareList) ||
        !readString(*identity, "software", envelope.identity.software) ||
        !readString(root, "source", envelope.source) ||
        !readNumber(root, "session", envelope.session)) {
        error = "missing or invalid live identity field";
        return false;
    }
    if (envelope.type == "snapshot" && !readNumber(root, "sequence", envelope.sequence)) {
        error = "missing or invalid live snapshot sequence";
        return false;
    }
    return true;
}

bool parseMessage(const std::string& line,
                  const std::string& expectedSource,
                  const std::vector<WatchRequest>& watchRequests,
                  Message& message,
                  std::string& error) {
    JsonValue root;
    if (!JsonParser(line).parse(root, error) || root.kind != JsonValue::Kind::Object) {
        if (error.empty()) error = "live message is not a JSON object";
        return false;
    }
    std::string protocol;
    std::uint64_t protocolVersion = 0;
    if (!readString(root, "protocol", protocol) || protocol != "openhi2txt-live" ||
        !readNumber(root, "protocolVersion", protocolVersion) || protocolVersion != 1 ||
        !readString(root, "type", message.type) ||
        (message.type != "hello" && message.type != "snapshot")) {
        error = "unsupported live protocol message";
        return false;
    }
    const JsonValue* identity = member(root, "identity");
    if (!identity || !readString(*identity, "machine", message.identity.machine) ||
        !readString(*identity, "softwareList", message.identity.softwareList) ||
        !readString(*identity, "software", message.identity.software) ||
        !readString(root, "source", message.source) ||
        !readNumber(root, "session", message.session)) {
        error = "missing or invalid live identity field";
        return false;
    }
    if (message.type == "hello") return true;

    std::string encoding;
    if (!readNumber(root, "sequence", message.sequence) ||
        !readString(root, "reason", message.reason) ||
        !readString(root, "encoding", encoding) || encoding != "hex" ||
        !sourceExpected(message.source, expectedSource, watchRequests)) {
        error = "unsupported snapshot source or encoding";
        return false;
    }

    const JsonValue* encodedRanges = member(root, "ranges");
    if (encodedRanges) {
        if (encodedRanges->kind != JsonValue::Kind::Array || watchRequests.empty() ||
            !readNumber(root, "sourceOffset", message.sourceOffset) ||
            !readNumber(root, "sourceSize", message.sourceSize)) {
            error = "invalid sparse snapshot";
            return false;
        }
        const auto request = std::find_if(watchRequests.begin(), watchRequests.end(), [&](const auto& item) {
            return item.source == message.source && item.sourceOffset == message.sourceOffset &&
                item.sourceSize == message.sourceSize;
        });
        if (request == watchRequests.end() || encodedRanges->array.size() != request->ranges.size()) {
            error = "sparse snapshot does not match its watch request";
            return false;
        }
        std::size_t transferred = 0;
        for (std::size_t index = 0; index < encodedRanges->array.size(); ++index) {
            const auto& encodedRange = encodedRanges->array[index];
            SnapshotRange range;
            std::uint64_t declaredLength = 0;
            std::string data;
            if (!readNumber(encodedRange, "offset", range.offset) ||
                !readNumber(encodedRange, "length", declaredLength) ||
                !readString(encodedRange, "data", data) ||
                range.offset != request->ranges[index].offset ||
                declaredLength != request->ranges[index].length ||
                declaredLength > kMaximumSnapshotBytes - transferred ||
                !decodeHex(data, range.bytes) || range.bytes.size() != declaredLength) {
                error = "sparse snapshot range does not match its watch request";
                return false;
            }
            transferred += range.bytes.size();
            message.ranges.push_back(std::move(range));
        }
        return true;
    }

    if (!watchRequests.empty()) {
        error = "expected a sparse snapshot for the selected live source";
        return false;
    }
    std::uint64_t declaredSize = 0;
    std::string data;
    if (!readNumber(root, "size", declaredSize) || declaredSize > kMaximumSnapshotBytes ||
        !readString(root, "data", data) || !decodeHex(data, message.bytes) ||
        message.bytes.size() != declaredSize) {
        error = "snapshot data does not match its declared size";
        return false;
    }
    return true;
}

std::string makeWatchRequest(const MameRuntimeIdentity& identity,
                             const std::vector<WatchRequest>& requests) {
    std::ostringstream output;
    output << "{\"protocol\":\"openhi2txt-live\",\"protocolVersion\":1,\"type\":\"watch\","
           << "\"identity\":{\"machine\":" << quote(identity.machine)
           << ",\"softwareList\":" << quote(identity.softwareList)
           << ",\"software\":" << quote(identity.software) << "},\"sources\":[";
    for (std::size_t index = 0; index < requests.size(); ++index) {
        if (index) output << ',';
        const auto& request = requests[index];
        output << "{\"source\":" << quote(request.source)
               << ",\"storage\":" << quote(request.storage) << ",\"storageNames\":[";
        for (std::size_t name = 0; name < request.storageNames.size(); ++name) {
            if (name) output << ',';
            output << quote(request.storageNames[name]);
        }
        output << "],\"sourceOffset\":" << request.sourceOffset
               << ",\"sourceSize\":" << request.sourceSize << ",\"ranges\":[";
        for (std::size_t range = 0; range < request.ranges.size(); ++range) {
            if (range) output << ',';
            output << "{\"offset\":" << request.ranges[range].offset
                   << ",\"length\":" << request.ranges[range].length << '}';
        }
        output << "]}";
    }
    output << "]}\n";
    return output.str();
}

} // namespace openhi2txt::live_detail
