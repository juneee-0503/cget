#include "cget/persistence/json.h"

#include <cmath>
#include <cstddef>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace cget::json {
namespace {

class Parser {
public:
    explicit Parser(const std::string& input) : input_(input) {}

    Value parse() {
        skipWhitespace();
        Value value = parseValue();
        skipWhitespace();
        if (!isEnd()) {
            fail("unexpected trailing input");
        }
        return value;
    }

private:
    [[nodiscard]] bool isEnd() const { return pos_ >= input_.size(); }

    [[nodiscard]] char peek() const {
        if (isEnd()) {
            return '\0';
        }
        return input_[pos_];
    }

    char consume() {
        if (isEnd()) {
            fail("unexpected end of input");
        }
        return input_[pos_++];
    }

    bool match(char expected) {
        if (peek() != expected) {
            return false;
        }
        ++pos_;
        return true;
    }

    void skipWhitespace() {
        while (!isEnd()) {
            const char c = peek();
            if (c != ' ' && c != '\n' && c != '\r' && c != '\t') {
                break;
            }
            ++pos_;
        }
    }

    [[noreturn]] void fail(const std::string& message) const {
        throw std::runtime_error("json parse error at byte " + std::to_string(pos_) + ": " + message);
    }

    Value parseValue() {
        skipWhitespace();
        switch (peek()) {
            case 'n': return parseLiteral("null", Value(nullptr));
            case 't': return parseLiteral("true", Value(true));
            case 'f': return parseLiteral("false", Value(false));
            case '"': return Value(parseString());
            case '[': return Value(parseArray());
            case '{': return Value(parseObject());
            default:
                if (peek() == '-' || (peek() >= '0' && peek() <= '9')) {
                    return Value(parseNumber());
                }
                fail("expected value");
        }
    }

    Value parseLiteral(const char* literal, Value value) {
        const std::string expected(literal);
        if (input_.compare(pos_, expected.size(), expected) != 0) {
            fail("expected " + expected);
        }
        pos_ += expected.size();
        return value;
    }

    std::string parseString() {
        if (!match('"')) {
            fail("expected string");
        }
        std::string result;
        while (!isEnd()) {
            const char c = consume();
            if (c == '"') {
                return result;
            }
            if (c != '\\') {
                result.push_back(c);
                continue;
            }
            const char escaped = consume();
            switch (escaped) {
                case '"': result.push_back('"'); break;
                case '\\': result.push_back('\\'); break;
                case '/': result.push_back('/'); break;
                case 'b': result.push_back('\b'); break;
                case 'f': result.push_back('\f'); break;
                case 'n': result.push_back('\n'); break;
                case 'r': result.push_back('\r'); break;
                case 't': result.push_back('\t'); break;
                default:
                    fail("unsupported escape sequence");
            }
        }
        fail("unterminated string");
    }

    double parseNumber() {
        const std::size_t start = pos_;
        if (peek() == '-') {
            ++pos_;
        }
        if (peek() == '0') {
            ++pos_;
        } else {
            while (peek() >= '0' && peek() <= '9') {
                ++pos_;
            }
        }
        if (peek() == '.') {
            ++pos_;
            while (peek() >= '0' && peek() <= '9') {
                ++pos_;
            }
        }
        if (peek() == 'e' || peek() == 'E') {
            ++pos_;
            if (peek() == '+' || peek() == '-') {
                ++pos_;
            }
            while (peek() >= '0' && peek() <= '9') {
                ++pos_;
            }
        }
        return std::stod(input_.substr(start, pos_ - start));
    }

    Value::Array parseArray() {
        Value::Array result;
        consume();
        skipWhitespace();
        if (match(']')) {
            return result;
        }
        while (true) {
            result.push_back(parseValue());
            skipWhitespace();
            if (match(']')) {
                return result;
            }
            if (!match(',')) {
                fail("expected ',' or ']'");
            }
        }
    }

    Value::Object parseObject() {
        Value::Object result;
        consume();
        skipWhitespace();
        if (match('}')) {
            return result;
        }
        while (true) {
            skipWhitespace();
            const std::string key = parseString();
            skipWhitespace();
            if (!match(':')) {
                fail("expected ':'");
            }
            result.emplace(key, parseValue());
            skipWhitespace();
            if (match('}')) {
                return result;
            }
            if (!match(',')) {
                fail("expected ',' or '}'");
            }
        }
    }

    const std::string& input_;
    std::size_t pos_ = 0;
};

void appendIndent(std::ostringstream& out, int count) {
    for (int i = 0; i < count; ++i) {
        out << ' ';
    }
}

void stringifyInto(std::ostringstream& out, const Value& value, int indent, int level) {
    if (value.isNull()) {
        out << "null";
    } else if (value.isBool()) {
        out << (value.asBool() ? "true" : "false");
    } else if (value.isNumber()) {
        const double number = value.asNumber();
        if (std::floor(number) == number && number >= 0.0) {
            out << static_cast<unsigned long long>(number);
        } else {
            out << std::setprecision(17) << number;
        }
    } else if (value.isString()) {
        out << '"' << escapeString(value.asString()) << '"';
    } else if (value.isArray()) {
        const auto& array = value.asArray();
        out << '[';
        if (!array.empty()) {
            out << '\n';
            for (std::size_t i = 0; i < array.size(); ++i) {
                appendIndent(out, level + indent);
                stringifyInto(out, array[i], indent, level + indent);
                if (i + 1 != array.size()) {
                    out << ',';
                }
                out << '\n';
            }
            appendIndent(out, level);
        }
        out << ']';
    } else {
        const auto& object = value.asObject();
        out << '{';
        if (!object.empty()) {
            out << '\n';
            std::size_t index = 0;
            for (const auto& [key, child] : object) {
                appendIndent(out, level + indent);
                out << '"' << escapeString(key) << "\": ";
                stringifyInto(out, child, indent, level + indent);
                if (++index != object.size()) {
                    out << ',';
                }
                out << '\n';
            }
            appendIndent(out, level);
        }
        out << '}';
    }
}

}  // namespace

Value::Value() : storage_(nullptr) {}
Value::Value(std::nullptr_t) : storage_(nullptr) {}
Value::Value(bool value) : storage_(value) {}
Value::Value(double value) : storage_(value) {}
Value::Value(int value) : storage_(static_cast<double>(value)) {}
Value::Value(std::uint64_t value) : storage_(static_cast<double>(value)) {}
Value::Value(std::string value) : storage_(std::move(value)) {}
Value::Value(const char* value) : storage_(std::string(value)) {}
Value::Value(Array value) : storage_(std::move(value)) {}
Value::Value(Object value) : storage_(std::move(value)) {}

bool Value::isNull() const { return std::holds_alternative<std::nullptr_t>(storage_); }
bool Value::isBool() const { return std::holds_alternative<bool>(storage_); }
bool Value::isNumber() const { return std::holds_alternative<double>(storage_); }
bool Value::isString() const { return std::holds_alternative<std::string>(storage_); }
bool Value::isArray() const { return std::holds_alternative<Array>(storage_); }
bool Value::isObject() const { return std::holds_alternative<Object>(storage_); }

bool Value::asBool() const { return std::get<bool>(storage_); }
double Value::asNumber() const { return std::get<double>(storage_); }

std::uint64_t Value::asUint64() const {
    const double value = asNumber();
    if (value < 0.0) {
        throw std::runtime_error("json number is negative");
    }
    return static_cast<std::uint64_t>(value);
}

const std::string& Value::asString() const { return std::get<std::string>(storage_); }
const Value::Array& Value::asArray() const { return std::get<Array>(storage_); }
const Value::Object& Value::asObject() const { return std::get<Object>(storage_); }

bool Value::contains(const std::string& key) const {
    if (!isObject()) {
        return false;
    }
    return asObject().find(key) != asObject().end();
}

const Value& Value::at(const std::string& key) const {
    const auto& object = asObject();
    const auto it = object.find(key);
    if (it == object.end()) {
        throw std::runtime_error("missing json key: " + key);
    }
    return it->second;
}

Value parse(const std::string& input) {
    return Parser(input).parse();
}

std::string stringify(const Value& value, int indent) {
    std::ostringstream out;
    stringifyInto(out, value, indent, 0);
    out << '\n';
    return out.str();
}

std::string escapeString(const std::string& value) {
    std::ostringstream out;
    for (const char c : value) {
        switch (c) {
            case '"': out << "\\\""; break;
            case '\\': out << "\\\\"; break;
            case '\b': out << "\\b"; break;
            case '\f': out << "\\f"; break;
            case '\n': out << "\\n"; break;
            case '\r': out << "\\r"; break;
            case '\t': out << "\\t"; break;
            default:
                out << c;
                break;
        }
    }
    return out.str();
}

}  // namespace cget::json
