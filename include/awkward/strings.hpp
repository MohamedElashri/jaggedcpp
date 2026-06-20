#pragma once

#include "awkward/operations.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace ak::str {

struct SplitOptions {
    std::optional<std::size_t> max_splits;
};

namespace detail {

inline bool ascii_alpha(unsigned char value) noexcept {
    return (value >= 'A' && value <= 'Z') || (value >= 'a' && value <= 'z');
}

inline bool ascii_upper(unsigned char value) noexcept {
    return value >= 'A' && value <= 'Z';
}

inline bool ascii_lower(unsigned char value) noexcept {
    return value >= 'a' && value <= 'z';
}

inline bool ascii_digit(unsigned char value) noexcept {
    return value >= '0' && value <= '9';
}

inline bool ascii_space(unsigned char value) noexcept {
    return value == ' ' || value == '\t' || value == '\n' || value == '\r' || value == '\f' || value == '\v';
}

inline unsigned char ascii_to_lower(unsigned char value) noexcept {
    return ascii_upper(value) ? static_cast<unsigned char>(value + ('a' - 'A')) : value;
}

inline unsigned char ascii_to_upper(unsigned char value) noexcept {
    return ascii_lower(value) ? static_cast<unsigned char>(value - ('a' - 'A')) : value;
}

inline bool all_ascii(std::string_view value) noexcept {
    return std::all_of(value.begin(), value.end(), [](unsigned char item) { return item <= 0x7f; });
}

template <typename Operation>
Value map_strings(const Value& value, std::string_view operation_name, const Operation& operation) {
    if (value.is_none()) {
        return Value(nullptr);
    }
    const auto tag = ak::detail::value_tag(value);
    if (tag == ak::detail::ValueTag::string) {
        return operation(std::get<std::string>(value.storage()));
    }
    if (tag == ak::detail::ValueTag::list) {
        Value::list_type result;
        result.reserve(value.as_list().size());
        for (const auto& item : value.as_list()) {
            result.push_back(map_strings(item, operation_name, operation));
        }
        return Value(std::move(result));
    }
    if (tag == ak::detail::ValueTag::record) {
        const auto& source = value.as_record();
        Value::record_type result;
        result.fields = source.fields;
        result.is_tuple = source.is_tuple;
        result.values.reserve(source.values.size());
        for (const auto& item : source.values) {
            result.values.push_back(map_strings(item, operation_name, operation));
        }
        return Value(std::move(result));
    }
    throw std::invalid_argument("ak::str::" + std::string(operation_name) + " requires string values");
}

template <typename Operation>
Array apply(const Array& array, std::string_view operation_name, const Operation& operation) {
    auto result = map_strings(array.to_list(), operation_name, operation);
    return ak::detail::attach_metadata(ak::detail::array_from_list(std::move(result.as_list())), array);
}

template <typename Predicate>
Array predicate(const Array& array, std::string_view name, const Predicate& predicate) {
    return apply(array, name, [&predicate](const std::string& value) { return Value(predicate(value)); });
}

template <typename Transform>
Array transform(const Array& array, std::string_view name, const Transform& transform) {
    return apply(array, name, [&transform](const std::string& value) { return Value(transform(value)); });
}

inline std::string join_strings(const Value::list_type& values, std::string_view separator) {
    std::string result;
    bool first = true;
    for (const auto& value : values) {
        if (value.is_none()) {
            throw std::invalid_argument("missing string");
        }
        if (ak::detail::value_tag(value) != ak::detail::ValueTag::string) {
            throw std::invalid_argument("ak::str::join requires lists containing only strings");
        }
        if (!first) {
            result.append(separator);
        }
        result.append(std::get<std::string>(value.storage()));
        first = false;
    }
    return result;
}

inline Value join_value(const Value& value, std::string_view separator) {
    if (value.is_none()) {
        return Value(nullptr);
    }
    if (ak::detail::value_tag(value) == ak::detail::ValueTag::record) {
        const auto& source = value.as_record();
        Value::record_type result;
        result.fields = source.fields;
        result.is_tuple = source.is_tuple;
        result.values.reserve(source.values.size());
        for (const auto& item : source.values) {
            result.values.push_back(join_value(item, separator));
        }
        return Value(std::move(result));
    }
    if (ak::detail::value_tag(value) != ak::detail::ValueTag::list) {
        throw std::invalid_argument("ak::str::join requires an array of string lists");
    }

    const auto& values = value.as_list();
    const bool is_string_list = std::all_of(values.begin(), values.end(), [](const Value& item) {
        return item.is_none() || ak::detail::value_tag(item) == ak::detail::ValueTag::string;
    });
    if (is_string_list) {
        if (std::any_of(values.begin(), values.end(), [](const Value& item) { return item.is_none(); })) {
            return Value(nullptr);
        }
        return Value(join_strings(values, separator));
    }

    Value::list_type result;
    result.reserve(values.size());
    for (const auto& item : values) {
        result.push_back(join_value(item, separator));
    }
    return Value(std::move(result));
}

}  // namespace detail

inline Array is_alpha(const Array& array) {
    return detail::predicate(array, "is_alpha", [](const std::string& value) {
        return !value.empty() && detail::all_ascii(value) &&
               std::all_of(value.begin(), value.end(), [](unsigned char item) { return detail::ascii_alpha(item); });
    });
}

inline Array is_alnum(const Array& array) {
    return detail::predicate(array, "is_alnum", [](const std::string& value) {
        return !value.empty() && detail::all_ascii(value) && std::all_of(value.begin(), value.end(), [](unsigned char item) {
                   return detail::ascii_alpha(item) || detail::ascii_digit(item);
               });
    });
}

inline Array is_ascii(const Array& array) {
    return detail::predicate(array, "is_ascii", [](const std::string& value) { return detail::all_ascii(value); });
}

inline Array is_decimal(const Array& array) {
    return detail::predicate(array, "is_decimal", [](const std::string& value) {
        return !value.empty() && detail::all_ascii(value) &&
               std::all_of(value.begin(), value.end(), [](unsigned char item) { return detail::ascii_digit(item); });
    });
}

inline Array is_digit(const Array& array) {
    return detail::predicate(array, "is_digit", [](const std::string& value) {
        return !value.empty() && detail::all_ascii(value) &&
               std::all_of(value.begin(), value.end(), [](unsigned char item) { return detail::ascii_digit(item); });
    });
}

inline Array is_numeric(const Array& array) {
    return detail::predicate(array, "is_numeric", [](const std::string& value) {
        return !value.empty() && detail::all_ascii(value) &&
               std::all_of(value.begin(), value.end(), [](unsigned char item) { return detail::ascii_digit(item); });
    });
}

inline Array is_space(const Array& array) {
    return detail::predicate(array, "is_space", [](const std::string& value) {
        return !value.empty() && detail::all_ascii(value) &&
               std::all_of(value.begin(), value.end(), [](unsigned char item) { return detail::ascii_space(item); });
    });
}

inline Array is_printable(const Array& array) {
    return detail::predicate(array, "is_printable", [](const std::string& value) {
        return detail::all_ascii(value) && std::all_of(value.begin(), value.end(), [](unsigned char item) {
                   return item >= 0x20 && item <= 0x7e;
               });
    });
}

inline Array is_lower(const Array& array) {
    return detail::predicate(array, "is_lower", [](const std::string& value) {
        if (!detail::all_ascii(value)) {
            return false;
        }
        bool has_lower = false;
        for (const auto item : value) {
            const auto byte = static_cast<unsigned char>(item);
            if (detail::ascii_upper(byte)) {
                return false;
            }
            has_lower = has_lower || detail::ascii_lower(byte);
        }
        return has_lower;
    });
}

inline Array is_upper(const Array& array) {
    return detail::predicate(array, "is_upper", [](const std::string& value) {
        if (!detail::all_ascii(value)) {
            return false;
        }
        bool has_upper = false;
        for (const auto item : value) {
            const auto byte = static_cast<unsigned char>(item);
            if (detail::ascii_lower(byte)) {
                return false;
            }
            has_upper = has_upper || detail::ascii_upper(byte);
        }
        return has_upper;
    });
}

inline Array is_title(const Array& array) {
    return detail::predicate(array, "is_title", [](const std::string& value) {
        if (!detail::all_ascii(value)) {
            return false;
        }
        bool expect_upper = true;
        bool has_cased = false;
        for (const auto item : value) {
            const auto byte = static_cast<unsigned char>(item);
            if (!detail::ascii_alpha(byte)) {
                expect_upper = true;
                continue;
            }
            if ((expect_upper && !detail::ascii_upper(byte)) || (!expect_upper && !detail::ascii_lower(byte))) {
                return false;
            }
            expect_upper = false;
            has_cased = true;
        }
        return has_cased;
    });
}

inline Array lower(const Array& array) {
    return detail::transform(array, "lower", [](std::string value) {
        for (auto& item : value) {
            item = static_cast<char>(detail::ascii_to_lower(static_cast<unsigned char>(item)));
        }
        return value;
    });
}

inline Array upper(const Array& array) {
    return detail::transform(array, "upper", [](std::string value) {
        for (auto& item : value) {
            item = static_cast<char>(detail::ascii_to_upper(static_cast<unsigned char>(item)));
        }
        return value;
    });
}

inline Array capitalize(const Array& array) {
    return detail::transform(array, "capitalize", [](std::string value) {
        if (!value.empty()) {
            value.front() = static_cast<char>(detail::ascii_to_upper(static_cast<unsigned char>(value.front())));
            for (std::size_t i = 1; i < value.size(); ++i) {
                value[i] = static_cast<char>(detail::ascii_to_lower(static_cast<unsigned char>(value[i])));
            }
        }
        return value;
    });
}

inline Array title(const Array& array) {
    return detail::transform(array, "title", [](std::string value) {
        bool start_of_word = true;
        for (auto& item : value) {
            const auto byte = static_cast<unsigned char>(item);
            if (detail::ascii_alpha(byte)) {
                item = static_cast<char>(start_of_word ? detail::ascii_to_upper(byte) : detail::ascii_to_lower(byte));
                start_of_word = false;
            } else {
                start_of_word = true;
            }
        }
        return value;
    });
}

inline Array reverse(const Array& array) {
    return detail::transform(array, "reverse", [](std::string value) {
        std::reverse(value.begin(), value.end());
        return value;
    });
}

inline Array slice(const Array& array,
                   std::optional<std::ptrdiff_t> start = std::nullopt,
                   std::optional<std::ptrdiff_t> stop = std::nullopt,
                   std::ptrdiff_t step = 1) {
    return detail::transform(array, "slice", [start, stop, step](const std::string& value) {
        std::string result;
        for (const auto index : index::detail::indices_for_slice(index::Slice{start, stop, step}, value.size())) {
            result.push_back(value[index]);
        }
        return result;
    });
}

inline Array split_pattern(const Array& array, std::string_view pattern, SplitOptions options = {}) {
    if (pattern.empty()) {
        throw std::invalid_argument("ak::str::split_pattern requires a non-empty pattern");
    }
    return detail::apply(array, "split_pattern", [pattern, options](const std::string& value) {
        Value::list_type pieces;
        std::size_t begin = 0;
        std::size_t splits = 0;
        while (!options.max_splits || splits < *options.max_splits) {
            const auto found = value.find(pattern, begin);
            if (found == std::string::npos) {
                break;
            }
            pieces.emplace_back(value.substr(begin, found - begin));
            begin = found + pattern.size();
            ++splits;
        }
        pieces.emplace_back(value.substr(begin));
        return Value(std::move(pieces));
    });
}

inline Array join(const Array& array, std::string_view separator) {
    const auto source = array.to_list();
    const auto& top = source.as_list();
    Value::list_type result;
    result.reserve(top.size());
    for (const auto& item : top) {
        result.push_back(detail::join_value(item, separator));
    }
    return ak::detail::attach_metadata(ak::detail::array_from_list(std::move(result)), array);
}

inline Array contains(const Array& array, std::string_view pattern) {
    return detail::predicate(array, "contains", [pattern](const std::string& value) {
        return value.find(pattern) != std::string::npos;
    });
}

inline Array starts_with(const Array& array, std::string_view pattern) {
    return detail::predicate(array, "starts_with", [pattern](const std::string& value) {
        return value.starts_with(pattern);
    });
}

inline Array ends_with(const Array& array, std::string_view pattern) {
    return detail::predicate(array, "ends_with", [pattern](const std::string& value) {
        return value.ends_with(pattern);
    });
}

}  // namespace ak::str
