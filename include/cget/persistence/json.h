#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <variant>
#include <vector>

namespace cget::json {

class Value {
public:
    using Array = std::vector<Value>;
    using Object = std::map<std::string, Value>;
    using Storage = std::variant<std::nullptr_t, bool, double, std::string, Array, Object>;

    Value();
    Value(std::nullptr_t);
    Value(bool value);
    Value(double value);
    Value(int value);
    Value(std::uint64_t value);
    Value(std::string value);
    Value(const char* value);
    Value(Array value);
    Value(Object value);

    [[nodiscard]] bool isNull() const;
    [[nodiscard]] bool isBool() const;
    [[nodiscard]] bool isNumber() const;
    [[nodiscard]] bool isString() const;
    [[nodiscard]] bool isArray() const;
    [[nodiscard]] bool isObject() const;

    [[nodiscard]] bool asBool() const;
    [[nodiscard]] double asNumber() const;
    [[nodiscard]] std::uint64_t asUint64() const;
    [[nodiscard]] const std::string& asString() const;
    [[nodiscard]] const Array& asArray() const;
    [[nodiscard]] const Object& asObject() const;

    [[nodiscard]] bool contains(const std::string& key) const;
    [[nodiscard]] const Value& at(const std::string& key) const;

private:
    Storage storage_;
};

[[nodiscard]] Value parse(const std::string& input);
[[nodiscard]] std::string stringify(const Value& value, int indent = 2);
[[nodiscard]] std::string escapeString(const std::string& value);

}  // namespace cget::json
