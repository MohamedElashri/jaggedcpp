#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace ak {

class Array;
class Content;

namespace index {

struct Integer {
    std::ptrdiff_t value;
};

struct Slice {
    std::optional<std::ptrdiff_t> start;
    std::optional<std::ptrdiff_t> stop;
    std::ptrdiff_t step{1};
};

struct Ellipsis {};
struct NewAxis {};

struct Field {
    std::string name;
};

struct Fields {
    std::vector<std::string> names;
};

struct IntegerArray {
    std::vector<std::ptrdiff_t> values;
};

struct BooleanArray {
    std::vector<bool> values;
};

struct ArrayIndex {
    std::shared_ptr<const Content> layout;
};

using Item = std::variant<Integer, Slice, Ellipsis, NewAxis, Field, Fields, IntegerArray, BooleanArray, ArrayIndex>;

inline Item at(std::ptrdiff_t value) {
    return Integer{value};
}

inline Item range(std::optional<std::ptrdiff_t> start,
                  std::optional<std::ptrdiff_t> stop,
                  std::ptrdiff_t step = 1) {
    return Slice{start, stop, step};
}

inline Item range(std::ptrdiff_t start, std::ptrdiff_t stop, std::ptrdiff_t step = 1) {
    return range(std::optional<std::ptrdiff_t>{start}, std::optional<std::ptrdiff_t>{stop}, step);
}

inline Item all() {
    return Slice{std::nullopt, std::nullopt, 1};
}

inline Item ellipsis() {
    return Ellipsis{};
}

inline Item newaxis() {
    return NewAxis{};
}

inline Item field(std::string name) {
    return Field{std::move(name)};
}

inline Item fields(std::vector<std::string> names) {
    return Fields{std::move(names)};
}

inline Item integers(std::vector<std::ptrdiff_t> values) {
    return IntegerArray{std::move(values)};
}

inline Item booleans(std::vector<bool> values) {
    return BooleanArray{std::move(values)};
}

namespace detail {

inline std::size_t normalize_integer(std::ptrdiff_t index, std::size_t length, const char* what) {
    const auto signed_length = static_cast<std::ptrdiff_t>(length);
    const auto normalized = index < 0 ? signed_length + index : index;
    if (normalized < 0 || normalized >= signed_length) {
        throw std::out_of_range(std::string(what) + " index is out of range");
    }
    return static_cast<std::size_t>(normalized);
}

inline std::vector<std::size_t> indices_for_slice(const Slice& slice, std::size_t length) {
    if (slice.step == 0) {
        throw std::invalid_argument("slice step must not be zero");
    }

    std::vector<std::size_t> result;
    const auto n = static_cast<std::ptrdiff_t>(length);

    if (slice.step > 0) {
        auto start = slice.start.value_or(0);
        auto stop = slice.stop.value_or(n);
        if (start < 0) {
            start += n;
        }
        if (stop < 0) {
            stop += n;
        }
        start = std::max<std::ptrdiff_t>(0, std::min(start, n));
        stop = std::max<std::ptrdiff_t>(0, std::min(stop, n));
        for (auto i = start; i < stop; i += slice.step) {
            result.push_back(static_cast<std::size_t>(i));
        }
    } else {
        auto start = slice.start.value_or(n - 1);
        auto stop = slice.stop.value_or(-1);
        if (start < 0) {
            start += n;
        }
        if (slice.stop && stop < 0) {
            stop += n;
        }
        start = std::max<std::ptrdiff_t>(-1, std::min(start, n - 1));
        stop = std::max<std::ptrdiff_t>(-1, std::min(stop, n - 1));
        for (auto i = start; i > stop; i += slice.step) {
            result.push_back(static_cast<std::size_t>(i));
        }
    }

    return result;
}

inline std::vector<std::size_t> normalize_integer_array(const std::vector<std::ptrdiff_t>& values,
                                                        std::size_t length,
                                                        const char* what) {
    std::vector<std::size_t> result;
    result.reserve(values.size());
    for (const auto value : values) {
        result.push_back(normalize_integer(value, length, what));
    }
    return result;
}

}  // namespace detail

}  // namespace index

}  // namespace ak
