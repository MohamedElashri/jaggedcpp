#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace ak {

struct None {
    explicit constexpr None() = default;
};

inline constexpr None none{};

template <typename T>
class Option {
public:
    Option(None) noexcept : has_value_(false), value_{} {}
    Option(std::nullopt_t) noexcept : has_value_(false), value_{} {}
    Option(T value) : has_value_(true), value_(std::move(value)) {}

    bool has_value() const noexcept {
        return has_value_;
    }

    const T& value() const {
        if (!has_value_) {
            throw std::out_of_range("ak::Option does not contain a value");
        }
        return value_;
    }

private:
    bool has_value_;
    T value_;
};

class Value {
public:
    using list_type = std::vector<Value>;
    struct record_type {
        bool is_tuple{false};
        std::vector<std::string> fields;
        std::vector<Value> values;

        friend bool operator==(const record_type&, const record_type&) = default;
    };

    Value() = default;
    Value(std::nullptr_t) : value_(std::monostate{}) {}
    Value(bool value) : value_(value) {}
    Value(float value) : value_(static_cast<double>(value)) {}
    Value(double value) : value_(value) {}
    Value(const char* value) : value_(std::string(value)) {}
    Value(std::string value) : value_(std::move(value)) {}
    Value(list_type value) : value_(std::move(value)) {}
    Value(record_type value) : value_(std::move(value)) {}

    template <typename T>
    Value(T value) requires (std::is_integral_v<T> && !std::same_as<std::remove_cv_t<T>, bool>)
        : value_(static_cast<std::int64_t>(value)) {}

    const auto& storage() const noexcept {
        return value_;
    }

    bool is_none() const noexcept {
        return std::holds_alternative<std::monostate>(value_);
    }

    template <typename T>
    bool is() const noexcept {
        return std::holds_alternative<T>(value_);
    }

    template <typename T>
    const T& get() const {
        if (!is<T>()) {
            throw std::invalid_argument("ak::Value does not contain the requested C++ type");
        }
        return std::get<T>(value_);
    }

    const list_type& as_list() const {
        return std::get<list_type>(value_);
    }

    const record_type& as_record() const {
        return std::get<record_type>(value_);
    }

    friend bool operator==(const Value&, const Value&) = default;

private:
    std::variant<std::monostate, bool, std::int64_t, double, std::string, list_type, record_type> value_;
};

namespace detail {

inline void append_escaped(std::ostream& stream, const std::string& value) {
    stream << '"';
    for (const auto character : value) {
        if (character == '"' || character == '\\') {
            stream << '\\';
        }
        stream << character;
    }
    stream << '"';
}

inline void print_value(std::ostream& stream, const Value& value) {
    std::visit(
        [&stream](const auto& item) {
            using Item = std::decay_t<decltype(item)>;
            if constexpr (std::same_as<Item, std::monostate>) {
                stream << "None";
            } else if constexpr (std::same_as<Item, bool>) {
                stream << (item ? "true" : "false");
            } else if constexpr (std::same_as<Item, std::string>) {
                append_escaped(stream, item);
            } else if constexpr (std::same_as<Item, Value::list_type>) {
                stream << '[';
                for (std::size_t i = 0; i < item.size(); ++i) {
                    if (i != 0) {
                        stream << ", ";
                    }
                    print_value(stream, item[i]);
                }
                stream << ']';
            } else if constexpr (std::same_as<Item, Value::record_type>) {
                stream << (item.is_tuple ? '(' : '{');
                for (std::size_t i = 0; i < item.values.size(); ++i) {
                    if (i != 0) {
                        stream << ", ";
                    }
                    if (!item.is_tuple) {
                        stream << item.fields[i] << ": ";
                    }
                    print_value(stream, item.values[i]);
                }
                if (item.is_tuple && item.values.size() == 1) {
                    stream << ',';
                }
                stream << (item.is_tuple ? ')' : '}');
            } else {
                stream << item;
            }
        },
        value.storage());
}

}  // namespace detail

inline std::ostream& operator<<(std::ostream& stream, const Value& value) {
    detail::print_value(stream, value);
    return stream;
}

inline std::string to_string(const Value& value) {
    std::ostringstream stream;
    stream << value;
    return stream.str();
}

}  // namespace ak
