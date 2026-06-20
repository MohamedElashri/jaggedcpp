#pragma once

#include "awkward/value.hpp"

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace ak::detail {

template <typename T>
struct is_string_like
    : std::bool_constant<std::is_convertible_v<T, std::string_view> &&
                         !std::same_as<std::remove_cvref_t<T>, std::string_view>> {};

template <>
struct is_string_like<std::string> : std::true_type {};

template <typename T>
inline constexpr bool is_string_like_v = is_string_like<T>::value;

template <typename T>
using storage_type_t = std::conditional_t<is_string_like_v<T> && !std::same_as<std::remove_cvref_t<T>, std::string>,
                                          std::string,
                                          std::remove_cvref_t<T>>;

template <typename T>
storage_type_t<T> normalize_value(const T& value) {
    if constexpr (is_string_like_v<T> && !std::same_as<std::remove_cvref_t<T>, std::string>) {
        return std::string(value);
    } else {
        return value;
    }
}

template <typename T>
Value scalar_to_value(const T& value) {
    if constexpr (is_string_like_v<T>) {
        return Value(std::string(value));
    } else {
        return Value(value);
    }
}

template <typename T>
std::string primitive_type_name() {
    using Type = std::remove_cvref_t<T>;
    if constexpr (std::same_as<Type, bool>) {
        return "bool";
    } else if constexpr (std::is_integral_v<Type> && std::is_signed_v<Type>) {
        return "int64";
    } else if constexpr (std::is_integral_v<Type> && std::is_unsigned_v<Type>) {
        return "uint64";
    } else if constexpr (std::same_as<Type, float>) {
        return "float32";
    } else if constexpr (std::same_as<Type, double>) {
        return "float64";
    } else if constexpr (is_string_like_v<Type>) {
        return "string";
    } else {
        return "unknown";
    }
}

template <typename T>
std::size_t primitive_nbytes(const std::vector<T>& values) {
    using Type = std::remove_cvref_t<T>;
    if constexpr (std::same_as<Type, bool>) {
        return values.size() * sizeof(bool);
    } else if constexpr (std::is_integral_v<Type>) {
        return values.size() * sizeof(std::int64_t);
    } else if constexpr (std::same_as<Type, float>) {
        return values.size() * sizeof(float);
    } else if constexpr (std::same_as<Type, double>) {
        return values.size() * sizeof(double);
    } else if constexpr (is_string_like_v<Type>) {
        std::size_t total = 0;
        for (const auto& value : values) {
            total += value.size();
        }
        return total + (values.size() + 1) * sizeof(std::int64_t);
    } else {
        return values.size() * sizeof(Type);
    }
}

inline std::string list_type_string(std::size_t length, const std::string& item_type) {
    return std::to_string(length) + " * var * " + item_type;
}

inline std::string item_type_from_typestr(const std::string& typestr) {
    const auto separator = typestr.find(" * ");
    if (separator == std::string::npos) {
        return typestr;
    }
    return typestr.substr(separator + 3);
}

template <typename T>
Value vector_to_value(const std::vector<T>& values) {
    Value::list_type list;
    list.reserve(values.size());
    for (const auto& value : values) {
        list.emplace_back(scalar_to_value(value));
    }
    return Value(std::move(list));
}

template <typename T>
Value span_to_value(std::span<const T> values) {
    Value::list_type list;
    list.reserve(values.size());
    for (const auto& value : values) {
        list.emplace_back(scalar_to_value(value));
    }
    return Value(std::move(list));
}

}  // namespace ak::detail
