#pragma once

#include "awkward/array.hpp"
#include "awkward/contents/list_offset_array.hpp"
#include "awkward/contents/numpy_array.hpp"
#include "awkward/contents/regular_array.hpp"
#include "awkward/contents/union_array.hpp"
#include "awkward/value.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace ak::kernel {

enum class BinaryOperation {
    add,
    subtract,
    multiply,
    divide,
    equal,
    not_equal,
    less,
    less_equal,
    greater,
    greater_equal,
    logical_and,
    logical_or,
};

enum class Shape { flat, list_offset, regular };

struct NumericLayout {
    Shape shape{Shape::flat};
    std::vector<long double> values;
    std::vector<std::size_t> offsets;
    std::size_t regular_size{0};
    std::size_t regular_length{0};
    bool real{false};
};

template <typename T>
inline std::optional<NumericLayout> numeric_layout_as(const Content& layout) {
    if (const auto* flat = dynamic_cast<const NumpyArray<T>*>(&layout)) {
        NumericLayout result;
        result.values.reserve(flat->length());
        for (const auto value : flat->values_vector()) result.values.push_back(static_cast<long double>(value));
        result.real = std::is_floating_point_v<T>;
        return result;
    }
    if (const auto* list = dynamic_cast<const ListOffsetArray<T>*>(&layout)) {
        NumericLayout result;
        result.shape = Shape::list_offset;
        result.offsets.assign(list->offsets().begin(), list->offsets().end());
        result.values.reserve(list->content().length());
        for (const auto value : list->content().values_vector()) result.values.push_back(static_cast<long double>(value));
        result.real = std::is_floating_point_v<T>;
        return result;
    }
    if (const auto* regular = dynamic_cast<const RegularArray<T>*>(&layout)) {
        NumericLayout result;
        result.shape = Shape::regular;
        result.regular_size = regular->size();
        result.regular_length = regular->length();
        result.values.reserve(regular->content().length());
        for (const auto value : regular->content().values_vector()) result.values.push_back(static_cast<long double>(value));
        result.real = std::is_floating_point_v<T>;
        return result;
    }
    return std::nullopt;
}

inline std::optional<NumericLayout> numeric_layout(const Content& layout) {
    if (auto result = numeric_layout_as<bool>(layout)) return result;
    if (auto result = numeric_layout_as<char>(layout)) return result;
    if (auto result = numeric_layout_as<signed char>(layout)) return result;
    if (auto result = numeric_layout_as<unsigned char>(layout)) return result;
    if (auto result = numeric_layout_as<short>(layout)) return result;
    if (auto result = numeric_layout_as<unsigned short>(layout)) return result;
    if (auto result = numeric_layout_as<int>(layout)) return result;
    if (auto result = numeric_layout_as<unsigned int>(layout)) return result;
    if (auto result = numeric_layout_as<long>(layout)) return result;
    if (auto result = numeric_layout_as<unsigned long>(layout)) return result;
    if (auto result = numeric_layout_as<long long>(layout)) return result;
    if (auto result = numeric_layout_as<unsigned long long>(layout)) return result;
    if (auto result = numeric_layout_as<std::int64_t>(layout)) return result;
    if (auto result = numeric_layout_as<std::uint64_t>(layout)) return result;
    if (auto result = numeric_layout_as<float>(layout)) return result;
    if (auto result = numeric_layout_as<double>(layout)) return result;
    return std::nullopt;
}

inline bool same_shape(const NumericLayout& left, const NumericLayout& right) {
    return left.shape == right.shape && left.values.size() == right.values.size() &&
           left.offsets == right.offsets && left.regular_size == right.regular_size &&
           left.regular_length == right.regular_length;
}

inline bool boolean_result(BinaryOperation operation) {
    return operation == BinaryOperation::equal || operation == BinaryOperation::not_equal ||
           operation == BinaryOperation::less || operation == BinaryOperation::less_equal ||
           operation == BinaryOperation::greater || operation == BinaryOperation::greater_equal ||
           operation == BinaryOperation::logical_and || operation == BinaryOperation::logical_or;
}

inline long double apply(long double left, long double right, BinaryOperation operation) {
    switch (operation) {
    case BinaryOperation::add: return left + right;
    case BinaryOperation::subtract: return left - right;
    case BinaryOperation::multiply: return left * right;
    case BinaryOperation::divide: return left / right;
    case BinaryOperation::equal: return left == right;
    case BinaryOperation::not_equal: return left != right;
    case BinaryOperation::less: return left < right;
    case BinaryOperation::less_equal: return left <= right;
    case BinaryOperation::greater: return left > right;
    case BinaryOperation::greater_equal: return left >= right;
    case BinaryOperation::logical_and: return left != 0.0 && right != 0.0;
    case BinaryOperation::logical_or: return left != 0.0 || right != 0.0;
    }
    throw std::invalid_argument("unknown binary kernel operation");
}

template <typename T>
inline std::shared_ptr<const Content> build_layout(const NumericLayout& shape, std::vector<T> values) {
    if (shape.shape == Shape::flat) return std::make_shared<NumpyArray<T>>(std::move(values));
    if (shape.shape == Shape::list_offset) {
        return std::make_shared<ListOffsetArray<T>>(std::move(values), shape.offsets);
    }
    return std::make_shared<RegularArray<T>>(std::move(values), shape.regular_size, shape.regular_length);
}

inline std::shared_ptr<const Content> apply_numeric(const NumericLayout& shape,
                                                    const std::vector<long double>& right,
                                                    BinaryOperation operation,
                                                    bool real_result) {
    if (boolean_result(operation)) {
        std::vector<bool> values;
        values.reserve(shape.values.size());
        for (std::size_t i = 0; i < shape.values.size(); ++i) values.push_back(apply(shape.values[i], right[i], operation) != 0.0);
        return build_layout(shape, std::move(values));
    }
    if (real_result || operation == BinaryOperation::divide) {
        std::vector<double> values;
        values.reserve(shape.values.size());
        for (std::size_t i = 0; i < shape.values.size(); ++i) values.push_back(apply(shape.values[i], right[i], operation));
        return build_layout(shape, std::move(values));
    }
    std::vector<std::int64_t> values;
    values.reserve(shape.values.size());
    for (std::size_t i = 0; i < shape.values.size(); ++i) values.push_back(static_cast<std::int64_t>(apply(shape.values[i], right[i], operation)));
    return build_layout(shape, std::move(values));
}

inline std::shared_ptr<const Content> binary(const Content& left,
                                             const Content& right,
                                             BinaryOperation operation) {
    const auto left_values = numeric_layout(left);
    const auto right_values = numeric_layout(right);
    if (!left_values || !right_values || !same_shape(*left_values, *right_values)) return nullptr;
    return apply_numeric(*left_values, right_values->values, operation, left_values->real || right_values->real);
}

inline std::optional<std::pair<long double, bool>> numeric_scalar(const Value& value) {
    if (const auto* boolean = std::get_if<bool>(&value.storage())) return std::pair<long double, bool>{*boolean ? 1.0L : 0.0L, false};
    if (const auto* integer = std::get_if<std::int64_t>(&value.storage())) return std::pair<long double, bool>{static_cast<long double>(*integer), false};
    if (const auto* real = std::get_if<double>(&value.storage())) return std::pair<long double, bool>{*real, true};
    return std::nullopt;
}

inline std::shared_ptr<const Content> binary(const Content& layout,
                                             const Value& scalar,
                                             BinaryOperation operation,
                                             bool scalar_on_left = false) {
    const auto values = numeric_layout(layout);
    const auto scalar_value = numeric_scalar(scalar);
    if (!values || !scalar_value) return nullptr;
    std::vector<long double> scalar_values(values->values.size(), scalar_value->first);
    if (!scalar_on_left) return apply_numeric(*values, scalar_values, operation, values->real || scalar_value->second);
    NumericLayout scalar_shape = *values;
    scalar_shape.values = std::move(scalar_values);
    return apply_numeric(scalar_shape, values->values, operation, values->real || scalar_value->second);
}

inline std::shared_ptr<const Content> logical_not(const Content& layout) {
    const auto values = numeric_layout(layout);
    if (!values) return nullptr;
    std::vector<bool> result;
    result.reserve(values->values.size());
    for (const auto value : values->values) result.push_back(value == 0.0);
    return build_layout(*values, std::move(result));
}

template <typename T>
inline std::shared_ptr<const Content> concatenate_typed(const std::vector<Array>& arrays) {
    bool all_flat = true;
    bool all_list = true;
    std::vector<T> values;
    std::vector<std::size_t> offsets{0};
    for (const auto& array : arrays) {
        if (const auto* flat = dynamic_cast<const NumpyArray<T>*>(&array.layout())) {
            values.insert(values.end(), flat->values_vector().begin(), flat->values_vector().end());
            all_list = false;
        } else if (const auto* list = dynamic_cast<const ListOffsetArray<T>*>(&array.layout())) {
            values.insert(values.end(), list->content().values_vector().begin(), list->content().values_vector().end());
            for (std::size_t i = 1; i < list->offsets().size(); ++i) offsets.push_back(offsets.back() + list->offsets()[i] - list->offsets()[i - 1]);
            all_flat = false;
        } else {
            return nullptr;
        }
    }
    if (all_flat) return std::make_shared<NumpyArray<T>>(std::move(values));
    if (all_list) return std::make_shared<ListOffsetArray<T>>(std::move(values), std::move(offsets));
    return nullptr;
}

inline std::shared_ptr<const Content> concatenate_axis0(const std::vector<Array>& arrays) {
    if (arrays.empty()) return nullptr;
    if (arrays.size() == 1) return arrays.front().layout_ptr();
    if (auto result = concatenate_typed<bool>(arrays)) return result;
    if (auto result = concatenate_typed<char>(arrays)) return result;
    if (auto result = concatenate_typed<signed char>(arrays)) return result;
    if (auto result = concatenate_typed<unsigned char>(arrays)) return result;
    if (auto result = concatenate_typed<short>(arrays)) return result;
    if (auto result = concatenate_typed<unsigned short>(arrays)) return result;
    if (auto result = concatenate_typed<int>(arrays)) return result;
    if (auto result = concatenate_typed<unsigned int>(arrays)) return result;
    if (auto result = concatenate_typed<long>(arrays)) return result;
    if (auto result = concatenate_typed<unsigned long>(arrays)) return result;
    if (auto result = concatenate_typed<long long>(arrays)) return result;
    if (auto result = concatenate_typed<unsigned long long>(arrays)) return result;
    if (auto result = concatenate_typed<std::int64_t>(arrays)) return result;
    if (auto result = concatenate_typed<std::uint64_t>(arrays)) return result;
    if (auto result = concatenate_typed<float>(arrays)) return result;
    if (auto result = concatenate_typed<double>(arrays)) return result;

    std::vector<std::uint8_t> tags;
    std::vector<std::ptrdiff_t> index;
    std::vector<std::shared_ptr<const Content>> contents;
    contents.reserve(arrays.size());
    for (std::size_t tag = 0; tag < arrays.size(); ++tag) {
        if (tag > 255) throw std::invalid_argument("axis-0 concatenation supports at most 256 layouts");
        contents.push_back(arrays[tag].layout_ptr());
        for (std::size_t i = 0; i < arrays[tag].length(); ++i) {
            tags.push_back(static_cast<std::uint8_t>(tag));
            index.push_back(static_cast<std::ptrdiff_t>(i));
        }
    }
    return std::make_shared<UnionArray>(std::move(tags), std::move(index), std::move(contents));
}

inline std::shared_ptr<const Content> carry(const Content& layout, const std::vector<std::ptrdiff_t>& indices) {
    return layout.take_rows(indices);
}

inline std::shared_ptr<const Content> mask(const Content& layout, const Content& mask) {
    return layout.mask_as_array(mask);
}

}  // namespace ak::kernel
