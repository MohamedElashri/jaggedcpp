#pragma once

#include "awkward/array.hpp"
#include "awkward/buffers.hpp"
#include "awkward/contents/empty_array.hpp"
#include "awkward/contents/indexed_array.hpp"
#include "awkward/contents/list_array.hpp"
#include "awkward/contents/list_offset_content_array.hpp"
#include "awkward/contents/list_offset_array.hpp"
#include "awkward/contents/numpy_array.hpp"
#include "awkward/contents/option_array.hpp"
#include "awkward/contents/record_array.hpp"
#include "awkward/contents/regular_array.hpp"
#include "awkward/contents/regular_content_array.hpp"
#include "awkward/contents/string_array.hpp"
#include "awkward/contents/union_array.hpp"
#include "awkward/kernels.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace ak {

namespace index {

inline Item array(const Array& array) {
    return ArrayIndex{array.layout_ptr()};
}

}  // namespace index

namespace detail {

enum class ValueTag {
    none,
    boolean,
    integer,
    real,
    string,
    list,
    record,
};

inline ValueTag value_tag(const Value& value) {
    return std::visit(
        [](const auto& item) {
            using Item = std::decay_t<decltype(item)>;
            if constexpr (std::same_as<Item, std::monostate>) {
                return ValueTag::none;
            } else if constexpr (std::same_as<Item, bool>) {
                return ValueTag::boolean;
            } else if constexpr (std::same_as<Item, std::int64_t>) {
                return ValueTag::integer;
            } else if constexpr (std::same_as<Item, double>) {
                return ValueTag::real;
            } else if constexpr (std::same_as<Item, std::string>) {
                return ValueTag::string;
            } else if constexpr (std::same_as<Item, Value::list_type>) {
                return ValueTag::list;
            } else {
                return ValueTag::record;
            }
        },
        value.storage());
}

inline bool is_scalar_or_none(const Value& value) {
    const auto tag = value_tag(value);
    return tag != ValueTag::list;
}

inline ValueTag merged_scalar_tag(const Value::list_type& values) {
    std::optional<ValueTag> tag;
    for (const auto& value : values) {
        const auto current = value_tag(value);
        if (current == ValueTag::none) {
            continue;
        }
        if (current == ValueTag::list) {
            throw std::invalid_argument("expected scalar values while building primitive content");
        }
        if (!tag) {
            tag = current;
            continue;
        }
        if ((*tag == ValueTag::integer && current == ValueTag::real) ||
            (*tag == ValueTag::real && current == ValueTag::integer)) {
            tag = ValueTag::real;
            continue;
        }
        if (*tag != current) {
            throw std::invalid_argument("mixed scalar values require union reconstruction");
        }
    }
    return tag.value_or(ValueTag::none);
}

template <typename T>
T value_as(const Value& value) {
    const auto& storage = value.storage();
    if constexpr (std::same_as<T, bool>) {
        return std::get<bool>(storage);
    } else if constexpr (std::same_as<T, std::int64_t>) {
        return std::get<std::int64_t>(storage);
    } else if constexpr (std::same_as<T, double>) {
        if (const auto* integer = std::get_if<std::int64_t>(&storage)) {
            return static_cast<double>(*integer);
        }
        return std::get<double>(storage);
    } else {
        return std::get<std::string>(storage);
    }
}

template <typename T>
std::shared_ptr<const Content> primitive_layout_from_values(const Value::list_type& values, bool has_none) {
    std::vector<T> content_values;
    std::vector<std::ptrdiff_t> index;
    content_values.reserve(values.size());
    index.reserve(values.size());

    for (const auto& value : values) {
        if (value.is_none()) {
            index.push_back(-1);
            continue;
        }
        index.push_back(static_cast<std::ptrdiff_t>(content_values.size()));
        content_values.push_back(value_as<T>(value));
    }

    std::shared_ptr<const Content> content;
    if constexpr (std::same_as<T, std::string>) {
        content = std::make_shared<StringArray>(content_values);
    } else {
        content = std::make_shared<NumpyArray<T>>(std::move(content_values));
    }
    if (has_none) {
        return std::make_shared<IndexedOptionArray>(std::move(index), content);
    }
    return content;
}

inline std::shared_ptr<const Content> layout_from_list(const Value::list_type& values);

inline std::string union_group_key(const Value& value) {
    switch (value_tag(value)) {
    case ValueTag::boolean:
        return "boolean";
    case ValueTag::integer:
    case ValueTag::real:
        return "number";
    case ValueTag::string:
        return "string";
    case ValueTag::list:
        return "list";
    case ValueTag::record: {
        const auto& record = value.as_record();
        std::string key = record.is_tuple ? "tuple" : "record";
        for (const auto& field : record.fields) {
            key += ':' + std::to_string(field.size()) + ':' + field;
        }
        return key;
    }
    case ValueTag::none:
        break;
    }
    throw std::invalid_argument("missing values do not have a union content group");
}

inline std::shared_ptr<const Content> union_layout_from_values(const Value::list_type& values) {
    struct Group {
        std::string key;
        Value::list_type values;
    };

    std::vector<Group> groups;
    std::vector<std::uint8_t> tags;
    std::vector<std::ptrdiff_t> index;
    std::vector<std::ptrdiff_t> option_index;
    bool has_none = false;
    tags.reserve(values.size());
    index.reserve(values.size());
    option_index.reserve(values.size());

    for (const auto& value : values) {
        if (value.is_none()) {
            has_none = true;
            option_index.push_back(-1);
            continue;
        }

        const auto key = union_group_key(value);
        auto group = std::find_if(groups.begin(), groups.end(), [&key](const auto& candidate) {
            return candidate.key == key;
        });
        if (group == groups.end()) {
            groups.push_back(Group{key, {}});
            group = std::prev(groups.end());
        }
        const auto tag = static_cast<std::size_t>(std::distance(groups.begin(), group));
        if (tag > std::numeric_limits<std::uint8_t>::max()) {
            throw std::invalid_argument("ak::UnionArray supports at most 256 contents");
        }
        tags.push_back(static_cast<std::uint8_t>(tag));
        index.push_back(static_cast<std::ptrdiff_t>(group->values.size()));
        group->values.push_back(value);
        option_index.push_back(static_cast<std::ptrdiff_t>(tags.size() - 1));
    }

    if (groups.size() < 2) {
        throw std::invalid_argument("union construction requires at least two content groups");
    }

    std::vector<std::shared_ptr<const Content>> contents;
    contents.reserve(groups.size());
    for (const auto& group : groups) {
        contents.push_back(layout_from_list(group.values));
    }

    std::shared_ptr<const Content> result =
        std::make_shared<UnionArray>(std::move(tags), std::move(index), std::move(contents));
    if (has_none) {
        result = std::make_shared<IndexedOptionArray>(std::move(option_index), std::move(result));
    }
    return result;
}

inline void validate_record_shape(const Value::record_type& expected, const Value::record_type& actual) {
    if (actual.is_tuple != expected.is_tuple || actual.fields != expected.fields) {
        throw std::invalid_argument("record values must have matching fields and tuple state");
    }
    if (actual.values.size() != expected.values.size()) {
        throw std::invalid_argument("record values must have matching field counts");
    }
}

inline std::shared_ptr<const Content> layout_from_record_values(const Value::list_type& values) {
    bool has_none = false;
    Value::list_type present_records;
    std::vector<std::ptrdiff_t> record_index;
    present_records.reserve(values.size());
    record_index.reserve(values.size());

    for (const auto& value : values) {
        if (value.is_none()) {
            has_none = true;
            record_index.push_back(-1);
            continue;
        }
        if (value_tag(value) != ValueTag::record) {
            throw std::invalid_argument("record content requires matching record values");
        }
        record_index.push_back(static_cast<std::ptrdiff_t>(present_records.size()));
        present_records.push_back(value);
    }

    if (present_records.empty()) {
        return std::make_shared<IndexedOptionArray>(
            std::vector<std::ptrdiff_t>(values.size(), -1), std::make_shared<EmptyArray>());
    }

    const auto& first = present_records.front().as_record();
    std::vector<Value::list_type> field_values(first.values.size());
    for (const auto& value : present_records) {
        const auto& record = value.as_record();
        validate_record_shape(first, record);
        for (std::size_t i = 0; i < field_values.size(); ++i) {
            field_values[i].push_back(record.values[i]);
        }
    }

    std::vector<std::shared_ptr<const Content>> contents;
    contents.reserve(field_values.size());
    for (auto& field : field_values) {
        contents.push_back(layout_from_list(field));
    }

    std::shared_ptr<const Content> records =
        std::make_shared<RecordArray>(first.fields, std::move(contents), first.is_tuple, std::string{}, present_records.size());
    if (has_none) {
        return std::make_shared<IndexedOptionArray>(std::move(record_index), records);
    }
    return records;
}

inline std::shared_ptr<const Content> scalar_layout_from_values(const Value::list_type& values) {
    if (values.empty()) {
        return std::make_shared<EmptyArray>();
    }
    bool has_none = false;
    for (const auto& value : values) {
        has_none = has_none || value.is_none();
    }

    switch (merged_scalar_tag(values)) {
    case ValueTag::none:
        return std::make_shared<IndexedOptionArray>(
            std::vector<std::ptrdiff_t>(values.size(), -1), std::make_shared<EmptyArray>());
    case ValueTag::boolean:
        return primitive_layout_from_values<bool>(values, has_none);
    case ValueTag::integer:
        return primitive_layout_from_values<std::int64_t>(values, has_none);
    case ValueTag::real:
        return primitive_layout_from_values<double>(values, has_none);
    case ValueTag::string:
        return primitive_layout_from_values<std::string>(values, has_none);
    case ValueTag::list:
    case ValueTag::record:
        break;
    }
    throw std::invalid_argument("unsupported scalar layout");
}

inline std::shared_ptr<const Content> layout_from_list_rows(const Value::list_type& values) {
    bool has_missing_rows = false;
    Value::list_type present_rows;
    std::vector<std::ptrdiff_t> row_index;
    present_rows.reserve(values.size());
    row_index.reserve(values.size());

    for (const auto& row : values) {
        if (row.is_none()) {
            has_missing_rows = true;
            row_index.push_back(-1);
            continue;
        }
        if (value_tag(row) != ValueTag::list) {
            throw std::invalid_argument("list content requires list values");
        }
        row_index.push_back(static_cast<std::ptrdiff_t>(present_rows.size()));
        present_rows.push_back(row);
    }

    std::vector<std::size_t> offsets;
    Value::list_type flat_values;
    offsets.reserve(present_rows.size() + 1);
    offsets.push_back(0);
    for (const auto& row : present_rows) {
        for (const auto& item : row.as_list()) {
            flat_values.push_back(item);
        }
        offsets.push_back(flat_values.size());
    }

    auto content = layout_from_list(flat_values);
    std::shared_ptr<const Content> rows;
    if (content->kind() == LayoutKind::numpy) {
        switch (merged_scalar_tag(flat_values)) {
        case ValueTag::boolean: {
            std::vector<bool> raw;
            raw.reserve(flat_values.size());
            for (const auto& value : flat_values) {
                raw.push_back(value_as<bool>(value));
            }
            rows = std::make_shared<ListOffsetArray<bool>>(std::move(raw), std::move(offsets));
            break;
        }
        case ValueTag::integer: {
            std::vector<std::int64_t> raw;
            raw.reserve(flat_values.size());
            for (const auto& value : flat_values) {
                raw.push_back(value_as<std::int64_t>(value));
            }
            rows = std::make_shared<ListOffsetArray<std::int64_t>>(std::move(raw), std::move(offsets));
            break;
        }
        case ValueTag::real: {
            std::vector<double> raw;
            raw.reserve(flat_values.size());
            for (const auto& value : flat_values) {
                raw.push_back(value_as<double>(value));
            }
            rows = std::make_shared<ListOffsetArray<double>>(std::move(raw), std::move(offsets));
            break;
        }
        case ValueTag::string: {
            std::vector<std::string> raw;
            raw.reserve(flat_values.size());
            for (const auto& value : flat_values) {
                raw.push_back(value_as<std::string>(value));
            }
            rows = std::make_shared<ListOffsetArray<std::string>>(std::move(raw), std::move(offsets));
            break;
        }
        case ValueTag::none:
        case ValueTag::list:
        case ValueTag::record:
            rows = std::make_shared<ListOffsetContentArray>(content, std::move(offsets));
            break;
        }
    } else {
        rows = std::make_shared<ListOffsetContentArray>(content, std::move(offsets));
    }

    if (has_missing_rows) {
        return std::make_shared<IndexedOptionArray>(std::move(row_index), rows);
    }
    return rows;
}

inline std::shared_ptr<const Content> layout_from_list(const Value::list_type& values) {
    bool has_list = false;
    bool has_scalar = false;
    bool has_record = false;
    for (const auto& value : values) {
        if (value.is_none()) {
            continue;
        }
        if (value_tag(value) == ValueTag::list) {
            has_list = true;
        } else if (value_tag(value) == ValueTag::record) {
            has_record = true;
        } else {
            has_scalar = true;
        }
    }
    const auto categories = static_cast<int>(has_list) + static_cast<int>(has_scalar) + static_cast<int>(has_record);
    if (categories > 1) {
        return union_layout_from_values(values);
    }

    if (categories == 1) {
        std::vector<std::string> groups;
        for (const auto& value : values) {
            if (value.is_none()) {
                continue;
            }
            const auto key = union_group_key(value);
            if (std::find(groups.begin(), groups.end(), key) == groups.end()) {
                groups.push_back(key);
            }
        }
        if (groups.size() > 1) {
            return union_layout_from_values(values);
        }
    }
    if (has_list) {
        return layout_from_list_rows(values);
    }
    if (has_record) {
        return layout_from_record_values(values);
    }
    return scalar_layout_from_values(values);
}

inline Array array_from_list(Value::list_type values) {
    return Array(layout_from_list(values));
}

inline Value concatenate_values(const std::vector<Value>& values, int axis, int depth = 0) {
    if (values.empty()) {
        return Value::list_type{};
    }
    if (depth == axis) {
        Value::list_type result;
        for (const auto& value : values) {
            if (value_tag(value) != ValueTag::list) {
                throw std::invalid_argument("ak::concatenate axis requires list values");
            }
            result.insert(result.end(), value.as_list().begin(), value.as_list().end());
        }
        return result;
    }

    for (const auto& value : values) {
        if (value_tag(value) != ValueTag::list) {
            throw std::invalid_argument("ak::concatenate axis is deeper than an input layout");
        }
    }
    const auto length = values.front().as_list().size();
    for (const auto& value : values) {
        if (value.as_list().size() != length) {
            throw std::invalid_argument("ak::concatenate requires matching lengths before the selected axis");
        }
    }

    Value::list_type result;
    result.reserve(length);
    for (std::size_t i = 0; i < length; ++i) {
        std::vector<Value> children;
        children.reserve(values.size());
        for (const auto& value : values) children.push_back(value.as_list()[i]);
        result.push_back(concatenate_values(children, axis, depth + 1));
    }
    return result;
}

inline Value num_at_axis(const Value& value, int axis, int depth = 0) {
    if (value.is_none()) return Value(nullptr);
    if (value_tag(value) != ValueTag::list) {
        throw std::invalid_argument("ak::num axis is deeper than an input value");
    }
    if (depth == axis) return static_cast<std::int64_t>(value.as_list().size());
    Value::list_type result;
    result.reserve(value.as_list().size());
    for (const auto& item : value.as_list()) result.push_back(num_at_axis(item, axis, depth + 1));
    return result;
}

inline Value flatten_at_axis(const Value& value, int axis, int depth = 0) {
    if (value.is_none()) return Value(nullptr);
    if (value_tag(value) != ValueTag::list) {
        throw std::invalid_argument("ak::flatten axis is deeper than an input value");
    }
    if (depth + 1 == axis) {
        Value::list_type result;
        for (const auto& item : value.as_list()) {
            if (item.is_none()) continue;
            if (value_tag(item) != ValueTag::list) {
                throw std::invalid_argument("ak::flatten requires list values at the selected axis");
            }
            result.insert(result.end(), item.as_list().begin(), item.as_list().end());
        }
        return result;
    }
    Value::list_type result;
    result.reserve(value.as_list().size());
    for (const auto& item : value.as_list()) result.push_back(flatten_at_axis(item, axis, depth + 1));
    return result;
}

inline void collect_ravel_values(const Value& value, Value::list_type& result) {
    if (value_tag(value) != ValueTag::list) {
        result.push_back(value);
        return;
    }
    for (const auto& item : value.as_list()) collect_ravel_values(item, result);
}

inline Value local_index_at_axis(const Value& value, int axis, int depth = 0) {
    if (value.is_none()) return Value(nullptr);
    if (value_tag(value) != ValueTag::list) {
        throw std::invalid_argument("ak::local_index axis is deeper than an input value");
    }
    if (depth == axis) {
        Value::list_type result;
        result.reserve(value.as_list().size());
        for (std::size_t i = 0; i < value.as_list().size(); ++i) {
            result.emplace_back(static_cast<std::int64_t>(i));
        }
        return result;
    }
    Value::list_type result;
    result.reserve(value.as_list().size());
    for (const auto& item : value.as_list()) result.push_back(local_index_at_axis(item, axis, depth + 1));
    return result;
}

inline Value::list_type require_top_list(const Array& array) {
    const auto value = array.to_list();
    if (value_tag(value) != ValueTag::list) {
        throw std::invalid_argument("operation requires an array list value");
    }
    return value.as_list();
}

inline Value is_none_value(const Value& value, int axis, int depth) {
    if (axis == depth) {
        return value.is_none();
    }
    if (value.is_none()) {
        return Value(nullptr);
    }
    if (value_tag(value) != ValueTag::list) {
        throw std::invalid_argument("ak::is_none axis is deeper than this layout");
    }
    Value::list_type result;
    for (const auto& item : value.as_list()) {
        result.push_back(is_none_value(item, axis, depth + 1));
    }
    return result;
}

inline bool should_drop_at_axis(const Value& value, std::optional<int> axis, int depth) {
    if (!value.is_none()) {
        return false;
    }
    return !axis || *axis == depth;
}

inline Value drop_none_value(const Value& value, std::optional<int> axis, int depth) {
    if (value.is_none()) {
        return Value(nullptr);
    }
    if (value_tag(value) != ValueTag::list) {
        return value;
    }
    Value::list_type result;
    for (const auto& item : value.as_list()) {
        if (!should_drop_at_axis(item, axis, depth + 1)) {
            result.push_back(drop_none_value(item, axis, depth + 1));
        }
    }
    return result;
}

inline Value fill_none_value(const Value& value, const Value& fill_value) {
    if (value.is_none()) {
        return fill_value;
    }
    if (value_tag(value) != ValueTag::list) {
        return value;
    }
    Value::list_type result;
    for (const auto& item : value.as_list()) {
        result.push_back(fill_none_value(item, fill_value));
    }
    return result;
}

inline Value pad_none_value(const Value& value, std::size_t target, int axis, bool clip, int depth) {
    if (value.is_none()) {
        return Value(nullptr);
    }
    if (value_tag(value) != ValueTag::list) {
        throw std::invalid_argument("ak::pad_none axis is deeper than this layout");
    }

    Value::list_type result;
    if (axis == depth) {
        result = value.as_list();
        if (clip && result.size() > target) {
            result.resize(target);
        }
        while (result.size() < target) {
            result.emplace_back(nullptr);
        }
        return result;
    }

    for (const auto& item : value.as_list()) {
        result.push_back(pad_none_value(item, target, axis, clip, depth + 1));
    }
    return result;
}

inline Value nan_to_none_value(const Value& value) {
    if (const auto* real = std::get_if<double>(&value.storage())) {
        if (std::isnan(*real)) {
            return Value(nullptr);
        }
        return value;
    }
    if (value_tag(value) != ValueTag::list) {
        return value;
    }
    Value::list_type result;
    for (const auto& item : value.as_list()) {
        result.push_back(nan_to_none_value(item));
    }
    return result;
}

inline Value nan_to_num_value(const Value& value, double nan, double posinf, double neginf) {
    if (const auto* real = std::get_if<double>(&value.storage())) {
        if (std::isnan(*real)) {
            return nan;
        }
        if (*real == std::numeric_limits<double>::infinity()) {
            return posinf;
        }
        if (*real == -std::numeric_limits<double>::infinity()) {
            return neginf;
        }
        return value;
    }
    if (value_tag(value) != ValueTag::list) {
        return value;
    }
    Value::list_type result;
    for (const auto& item : value.as_list()) {
        result.push_back(nan_to_num_value(item, nan, posinf, neginf));
    }
    return result;
}

inline Value mask_value(const Value& value, const Value& mask, bool valid_when) {
    if (value_tag(mask) == ValueTag::boolean) {
        const auto keep = std::get<bool>(mask.storage()) == valid_when;
        return keep ? value : Value(nullptr);
    }
    if (value_tag(value) != ValueTag::list || value_tag(mask) != ValueTag::list) {
        throw std::invalid_argument("ak::mask requires matching boolean mask structure");
    }
    const auto& values = value.as_list();
    const auto& masks = mask.as_list();
    if (values.size() != masks.size()) {
        throw std::invalid_argument("ak::mask requires matching mask lengths");
    }
    Value::list_type result;
    result.reserve(values.size());
    for (std::size_t i = 0; i < values.size(); ++i) {
        result.push_back(mask_value(values[i], masks[i], valid_when));
    }
    return result;
}

inline Value firsts_value(const Value& value, int axis, int depth) {
    if (value.is_none()) {
        return Value(nullptr);
    }
    if (value_tag(value) != ValueTag::list) {
        throw std::invalid_argument("ak::firsts axis is deeper than this layout");
    }
    if (axis == depth + 1) {
        Value::list_type result;
        for (const auto& row : value.as_list()) {
            if (row.is_none()) {
                result.emplace_back(nullptr);
                continue;
            }
            if (value_tag(row) != ValueTag::list) {
                throw std::invalid_argument("ak::firsts requires list values at the selected axis");
            }
            const auto& row_values = row.as_list();
            result.push_back(row_values.empty() ? Value(nullptr) : row_values.front());
        }
        return result;
    }
    Value::list_type result;
    for (const auto& item : value.as_list()) {
        result.push_back(firsts_value(item, axis, depth + 1));
    }
    return result;
}

inline Value singletons_value(const Value& value) {
    Value::list_type result;
    for (const auto& item : value.as_list()) {
        if (item.is_none()) {
            result.emplace_back(Value::list_type{});
        } else {
            result.emplace_back(Value::list_type{item});
        }
    }
    return result;
}

enum class ReducerKind {
    count,
    count_nonzero,
    sum,
    prod,
    any,
    all,
    min,
    max,
    argmin,
    argmax,
    mean,
    var,
    stddev,
    moment,
    ptp,
};

enum class BinaryOpKind {
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

inline kernel::BinaryOperation kernel_operation(BinaryOpKind kind) {
    switch (kind) {
    case BinaryOpKind::add: return kernel::BinaryOperation::add;
    case BinaryOpKind::subtract: return kernel::BinaryOperation::subtract;
    case BinaryOpKind::multiply: return kernel::BinaryOperation::multiply;
    case BinaryOpKind::divide: return kernel::BinaryOperation::divide;
    case BinaryOpKind::equal: return kernel::BinaryOperation::equal;
    case BinaryOpKind::not_equal: return kernel::BinaryOperation::not_equal;
    case BinaryOpKind::less: return kernel::BinaryOperation::less;
    case BinaryOpKind::less_equal: return kernel::BinaryOperation::less_equal;
    case BinaryOpKind::greater: return kernel::BinaryOperation::greater;
    case BinaryOpKind::greater_equal: return kernel::BinaryOperation::greater_equal;
    case BinaryOpKind::logical_and: return kernel::BinaryOperation::logical_and;
    case BinaryOpKind::logical_or: return kernel::BinaryOperation::logical_or;
    }
    throw std::invalid_argument("unknown binary operation");
}

inline bool is_numeric_tag(ValueTag tag) noexcept {
    return tag == ValueTag::boolean || tag == ValueTag::integer || tag == ValueTag::real;
}

inline int array_depth(const Value& value) {
    if (value.is_none() || value_tag(value) != ValueTag::list) {
        return 0;
    }
    int depth = 1;
    for (const auto& item : value.as_list()) {
        depth = std::max(depth, 1 + array_depth(item));
    }
    return depth;
}

inline int normalize_axis(std::optional<int> axis, int depth) {
    if (!axis) {
        return -1;
    }
    int normalized = *axis;
    if (normalized < 0) {
        normalized += depth;
    }
    if (normalized < 0 || normalized >= depth) {
        throw std::invalid_argument("reducer axis is outside the implemented layout depth");
    }
    return normalized;
}

inline bool is_nan_value(const Value& value) {
    if (const auto* real = std::get_if<double>(&value.storage())) {
        return std::isnan(*real);
    }
    return false;
}

inline bool is_missing_for_reducer(const Value& value, bool skip_nan) {
    return value.is_none() || (skip_nan && is_nan_value(value));
}

inline bool contains_real_scalar(const Value& value) {
    if (value_tag(value) == ValueTag::real) {
        return true;
    }
    if (value_tag(value) != ValueTag::list) {
        return false;
    }
    for (const auto& item : value.as_list()) {
        if (contains_real_scalar(item)) {
            return true;
        }
    }
    return false;
}

inline double numeric_as_double(const Value& value) {
    const auto& storage = value.storage();
    if (const auto* boolean = std::get_if<bool>(&storage)) {
        return *boolean ? 1.0 : 0.0;
    }
    if (const auto* integer = std::get_if<std::int64_t>(&storage)) {
        return static_cast<double>(*integer);
    }
    if (const auto* real = std::get_if<double>(&storage)) {
        return *real;
    }
    throw std::invalid_argument("numeric reducer received a non-numeric value");
}

inline bool value_truthy(const Value& value) {
    const auto tag = value_tag(value);
    if (tag == ValueTag::boolean) {
        return std::get<bool>(value.storage());
    }
    if (tag == ValueTag::integer) {
        return std::get<std::int64_t>(value.storage()) != 0;
    }
    if (tag == ValueTag::real) {
        return std::get<double>(value.storage()) != 0.0;
    }
    throw std::invalid_argument("truth-value reducer received a non-numeric value");
}

inline bool prefer_real_result(const Value& value) {
    return value_tag(value) == ValueTag::real;
}

inline Value numeric_result(double value, bool real_result) {
    if (real_result) {
        return value;
    }
    return static_cast<std::int64_t>(value);
}

struct ReduceSettings {
    ReducerKind kind;
    bool mask_identity{false};
    std::optional<Value> initial;
    bool skip_nan{false};
    double ddof{0.0};
    int moment_order{2};
};

inline void collect_scalars(const Value& value, Value::list_type& scalars, bool skip_nan) {
    if (is_missing_for_reducer(value, skip_nan)) {
        return;
    }
    if (value_tag(value) == ValueTag::list) {
        for (const auto& item : value.as_list()) {
            collect_scalars(item, scalars, skip_nan);
        }
        return;
    }
    scalars.push_back(value);
}

inline Value identity_value(ReducerKind kind, bool real_result) {
    switch (kind) {
    case ReducerKind::count:
    case ReducerKind::count_nonzero:
        return std::int64_t{0};
    case ReducerKind::sum:
        return numeric_result(0.0, real_result);
    case ReducerKind::prod:
        return numeric_result(1.0, real_result);
    case ReducerKind::any:
        return false;
    case ReducerKind::all:
        return true;
    case ReducerKind::min:
    case ReducerKind::max:
    case ReducerKind::argmin:
    case ReducerKind::argmax:
    case ReducerKind::mean:
    case ReducerKind::var:
    case ReducerKind::stddev:
    case ReducerKind::moment:
    case ReducerKind::ptp:
        return Value(nullptr);
    }
    return Value(nullptr);
}

inline Value reduce_scalars(Value::list_type values, const ReduceSettings& settings, bool real_result = false) {
    if (settings.initial) {
        real_result = real_result || prefer_real_result(*settings.initial);
    }
    for (const auto& value : values) {
        real_result = real_result || prefer_real_result(value);
    }

    if (settings.initial && !settings.initial->is_none()) {
        values.insert(values.begin(), *settings.initial);
    }

    switch (settings.kind) {
    case ReducerKind::count:
        if (values.empty() && settings.mask_identity) {
            return Value(nullptr);
        }
        return static_cast<std::int64_t>(values.size());
    case ReducerKind::count_nonzero: {
        if (values.empty() && settings.mask_identity) {
            return Value(nullptr);
        }
        std::int64_t count = 0;
        for (const auto& value : values) {
            if (value_truthy(value)) {
                ++count;
            }
        }
        return count;
    }
    case ReducerKind::sum: {
        if (values.empty() && settings.mask_identity) {
            return Value(nullptr);
        }
        double total = 0.0;
        for (const auto& value : values) {
            total += numeric_as_double(value);
        }
        return numeric_result(total, real_result);
    }
    case ReducerKind::prod: {
        if (values.empty() && settings.mask_identity) {
            return Value(nullptr);
        }
        double total = 1.0;
        for (const auto& value : values) {
            total *= numeric_as_double(value);
        }
        return numeric_result(total, real_result);
    }
    case ReducerKind::any: {
        if (values.empty() && settings.mask_identity) {
            return Value(nullptr);
        }
        return std::any_of(values.begin(), values.end(), value_truthy);
    }
    case ReducerKind::all: {
        if (values.empty() && settings.mask_identity) {
            return Value(nullptr);
        }
        return std::all_of(values.begin(), values.end(), value_truthy);
    }
    case ReducerKind::min:
    case ReducerKind::max: {
        if (values.empty()) {
            return identity_value(settings.kind, real_result);
        }
        auto best = numeric_as_double(values.front());
        for (std::size_t i = 1; i < values.size(); ++i) {
            const auto current = numeric_as_double(values[i]);
            if ((settings.kind == ReducerKind::min && current < best) ||
                (settings.kind == ReducerKind::max && current > best)) {
                best = current;
            }
        }
        return numeric_result(best, real_result);
    }
    case ReducerKind::argmin:
    case ReducerKind::argmax: {
        if (values.empty()) {
            return Value(nullptr);
        }
        auto best = numeric_as_double(values.front());
        std::int64_t best_index = 0;
        for (std::size_t i = 1; i < values.size(); ++i) {
            const auto current = numeric_as_double(values[i]);
            if ((settings.kind == ReducerKind::argmin && current < best) ||
                (settings.kind == ReducerKind::argmax && current > best)) {
                best = current;
                best_index = static_cast<std::int64_t>(i);
            }
        }
        return best_index;
    }
    case ReducerKind::mean:
    case ReducerKind::var:
    case ReducerKind::stddev:
    case ReducerKind::moment:
    case ReducerKind::ptp:
        break;
    }

    if (values.empty()) {
        return Value(nullptr);
    }

    double total = 0.0;
    for (const auto& value : values) {
        total += numeric_as_double(value);
    }
    const auto mean = total / static_cast<double>(values.size());

    if (settings.kind == ReducerKind::mean) {
        return mean;
    }
    if (settings.kind == ReducerKind::ptp) {
        auto low = numeric_as_double(values.front());
        auto high = low;
        for (std::size_t i = 1; i < values.size(); ++i) {
            const auto current = numeric_as_double(values[i]);
            low = std::min(low, current);
            high = std::max(high, current);
        }
        return high - low;
    }

    double powered = 0.0;
    for (const auto& value : values) {
        powered += std::pow(numeric_as_double(value) - mean, settings.moment_order);
    }
    if (settings.kind == ReducerKind::moment) {
        return powered / static_cast<double>(values.size());
    }

    const auto denominator = static_cast<double>(values.size()) - settings.ddof;
    if (denominator <= 0.0) {
        return Value(nullptr);
    }
    const auto variance = powered / denominator;
    if (settings.kind == ReducerKind::stddev) {
        return std::sqrt(variance);
    }
    return variance;
}

inline Value reduce_present_list(const Value::list_type& items, const ReduceSettings& settings) {
    Value::list_type scalars;
    scalars.reserve(items.size());
    bool skipped_real = false;
    for (const auto& item : items) {
        if (is_missing_for_reducer(item, settings.skip_nan)) {
            skipped_real = skipped_real || contains_real_scalar(item);
            continue;
        }
        if (value_tag(item) == ValueTag::list) {
            throw std::invalid_argument("selected reducer axis does not contain scalar values");
        }
        scalars.push_back(item);
    }
    return reduce_scalars(std::move(scalars), settings, skipped_real);
}

inline Value reduce_axis_none(const Value& value, const ReduceSettings& settings) {
    Value::list_type scalars;
    collect_scalars(value, scalars, settings.skip_nan);
    return reduce_scalars(std::move(scalars), settings, settings.skip_nan && contains_real_scalar(value));
}

inline Value reduce_axis_zero(const Value::list_type& values, const ReduceSettings& settings) {
    bool has_list = false;
    bool has_scalar = false;
    for (const auto& value : values) {
        if (is_missing_for_reducer(value, settings.skip_nan)) continue;
        if (value_tag(value) == ValueTag::list) has_list = true;
        else has_scalar = true;
    }
    if (has_list && has_scalar) {
        throw std::invalid_argument("axis reduction requires consistent list depth");
    }
    if (!has_list) return reduce_present_list(values, settings);

    std::size_t width = 0;
    for (const auto& value : values) {
        if (!is_missing_for_reducer(value, settings.skip_nan)) {
            width = std::max(width, value.as_list().size());
        }
    }
    Value::list_type result;
    result.reserve(width);
    for (std::size_t column = 0; column < width; ++column) {
        Value::list_type column_values;
        for (const auto& value : values) {
            if (is_missing_for_reducer(value, settings.skip_nan)) continue;
            const auto& items = value.as_list();
            if (column < items.size()) column_values.push_back(items[column]);
        }
        result.push_back(reduce_axis_zero(column_values, settings));
    }
    return result;
}

inline Value reduce_at_axis(const Value& value,
                            int target_axis,
                            int depth,
                            bool keepdims,
                            const ReduceSettings& settings) {
    if (value.is_none()) return Value(nullptr);
    if (value_tag(value) != ValueTag::list) {
        throw std::invalid_argument("reducer axis is deeper than an input value");
    }
    if (depth == target_axis) {
        auto result = reduce_axis_zero(value.as_list(), settings);
        if (keepdims) result = Value::list_type{std::move(result)};
        return result;
    }
    Value::list_type result;
    result.reserve(value.as_list().size());
    for (const auto& item : value.as_list()) {
        result.push_back(reduce_at_axis(item, target_axis, depth + 1, keepdims, settings));
    }
    return result;
}

inline Value reduce_array_value(const Value& array_value, std::optional<int> axis, bool keepdims, ReduceSettings settings) {
    const auto depth = array_depth(array_value);
    const auto normalized_axis = normalize_axis(axis, depth);
    if (normalized_axis == -1) {
        auto result = reduce_axis_none(array_value, settings);
        if (keepdims) {
            for (int i = 0; i < depth; ++i) result = Value::list_type{std::move(result)};
        }
        return result;
    }

    if (value_tag(array_value) != ValueTag::list) {
        throw std::invalid_argument("reducer requires an array value");
    }
    return reduce_at_axis(array_value, normalized_axis, 0, keepdims, settings);
}

inline bool sortable_less(const Value& left, const Value& right) {
    const auto left_tag = value_tag(left);
    const auto right_tag = value_tag(right);
    if (is_numeric_tag(left_tag) && is_numeric_tag(right_tag)) {
        return numeric_as_double(left) < numeric_as_double(right);
    }
    if (left_tag == ValueTag::string && right_tag == ValueTag::string) {
        return std::get<std::string>(left.storage()) < std::get<std::string>(right.storage());
    }
    throw std::invalid_argument("ak::sort requires comparable scalar values");
}

inline bool sortable_compare(const Value& left, const Value& right, bool ascending) {
    if (left.is_none() && right.is_none()) {
        return false;
    }
    if (left.is_none()) {
        return false;
    }
    if (right.is_none()) {
        return true;
    }
    return ascending ? sortable_less(left, right) : sortable_less(right, left);
}

inline Value::list_type sort_list(Value::list_type items, bool ascending) {
    std::stable_sort(items.begin(), items.end(), [ascending](const Value& left, const Value& right) {
        return sortable_compare(left, right, ascending);
    });
    return items;
}

inline Value::list_type argsort_list(const Value::list_type& items, bool ascending) {
    std::vector<std::size_t> indices(items.size());
    for (std::size_t i = 0; i < indices.size(); ++i) {
        indices[i] = i;
    }
    std::stable_sort(indices.begin(), indices.end(), [&items, ascending](std::size_t left, std::size_t right) {
        return sortable_compare(items[left], items[right], ascending);
    });

    Value::list_type result;
    result.reserve(indices.size());
    for (const auto index : indices) {
        result.emplace_back(static_cast<std::int64_t>(index));
    }
    return result;
}

inline Value::list_type sort_axis_zero_ragged(const Value::list_type& rows,
                                              bool ascending,
                                              bool return_indices) {
    Value::list_type result = rows;
    std::size_t width = 0;
    for (const auto& row : rows) {
        if (!row.is_none() && value_tag(row) == ValueTag::list) width = std::max(width, row.as_list().size());
    }
    for (std::size_t column = 0; column < width; ++column) {
        std::vector<std::size_t> target_rows;
        Value::list_type column_values;
        for (std::size_t row = 0; row < rows.size(); ++row) {
            if (rows[row].is_none() || value_tag(rows[row]) != ValueTag::list || column >= rows[row].as_list().size()) continue;
            target_rows.push_back(row);
            column_values.push_back(rows[row].as_list()[column]);
        }
        Value::list_type sorted;
        const auto nested = std::any_of(column_values.begin(), column_values.end(), [](const auto& value) {
            return !value.is_none() && value_tag(value) == ValueTag::list;
        });
        if (nested) {
            sorted = sort_axis_zero_ragged(column_values, ascending, return_indices);
        } else {
            sorted = return_indices ? argsort_list(column_values, ascending) : sort_list(column_values, ascending);
            if (return_indices) {
                for (auto& value : sorted) {
                    const auto local = static_cast<std::size_t>(std::get<std::int64_t>(value.storage()));
                    value = static_cast<std::int64_t>(target_rows[local]);
                }
            }
        }
        for (std::size_t i = 0; i < target_rows.size(); ++i) {
            auto row = result[target_rows[i]].as_list();
            row[column] = sorted[i];
            result[target_rows[i]] = std::move(row);
        }
    }
    return result;
}

inline Value sort_value(const Value& array_value, int axis, bool ascending, bool return_indices) {
    const auto depth = array_depth(array_value);
    auto normalized_axis = axis;
    if (normalized_axis < 0) {
        normalized_axis += depth;
    }
    if (normalized_axis < 0 || normalized_axis >= depth) {
        throw std::invalid_argument("sort axis is outside the implemented layout depth");
    }
    if (value_tag(array_value) != ValueTag::list) {
        throw std::invalid_argument("ak::sort requires an array value");
    }

    const auto recurse = [&](const auto& self, const Value& value, int current_depth) -> Value {
        if (value.is_none()) return Value(nullptr);
        if (value_tag(value) != ValueTag::list) {
            throw std::invalid_argument("sort axis is deeper than an input value");
        }
        if (current_depth == normalized_axis) {
            const auto nested = std::any_of(value.as_list().begin(), value.as_list().end(), [](const auto& item) {
                return !item.is_none() && value_tag(item) == ValueTag::list;
            });
            if (nested) return sort_axis_zero_ragged(value.as_list(), ascending, return_indices);
            return return_indices ? argsort_list(value.as_list(), ascending) : sort_list(value.as_list(), ascending);
        }
        Value::list_type result;
        result.reserve(value.as_list().size());
        for (const auto& item : value.as_list()) result.push_back(self(self, item, current_depth + 1));
        return result;
    };
    return recurse(recurse, array_value, 0);
}

inline Value softmax_list(const Value::list_type& items) {
    double maximum = -std::numeric_limits<double>::infinity();
    bool has_value = false;
    for (const auto& item : items) {
        if (is_missing_for_reducer(item, true)) {
            continue;
        }
        maximum = std::max(maximum, numeric_as_double(item));
        has_value = true;
    }
    if (!has_value) {
        Value::list_type empty_or_missing;
        empty_or_missing.reserve(items.size());
        for (const auto& item : items) {
            empty_or_missing.push_back(item.is_none() ? Value(nullptr) : Value(std::numeric_limits<double>::quiet_NaN()));
        }
        return empty_or_missing;
    }

    double denominator = 0.0;
    for (const auto& item : items) {
        if (!is_missing_for_reducer(item, true)) {
            denominator += std::exp(numeric_as_double(item) - maximum);
        }
    }

    Value::list_type result;
    result.reserve(items.size());
    for (const auto& item : items) {
        if (item.is_none()) {
            result.emplace_back(nullptr);
        } else if (is_nan_value(item)) {
            result.emplace_back(std::numeric_limits<double>::quiet_NaN());
        } else {
            result.emplace_back(std::exp(numeric_as_double(item) - maximum) / denominator);
        }
    }
    return result;
}

inline Value softmax_value(const Value& array_value, int axis) {
    const auto depth = array_depth(array_value);
    auto normalized_axis = axis;
    if (normalized_axis < 0) {
        normalized_axis += depth;
    }
    if (normalized_axis < 0 || normalized_axis >= depth) {
        throw std::invalid_argument("softmax axis is outside the implemented layout depth");
    }
    if (value_tag(array_value) != ValueTag::list) {
        throw std::invalid_argument("ak::softmax requires an array value");
    }

    const auto recurse = [&](const auto& self, const Value& value, int current_depth) -> Value {
        if (value.is_none()) return Value(nullptr);
        if (value_tag(value) != ValueTag::list) {
            throw std::invalid_argument("softmax axis is deeper than an input value");
        }
        if (current_depth == normalized_axis) return softmax_list(value.as_list());
        Value::list_type result;
        result.reserve(value.as_list().size());
        for (const auto& item : value.as_list()) result.push_back(self(self, item, current_depth + 1));
        return result;
    };
    return recurse(recurse, array_value, 0);
}

inline Value make_record_value(const std::vector<std::string>& names,
                               const std::vector<Value>& values,
                               bool is_tuple = false) {
    if (names.size() != values.size()) {
        throw std::invalid_argument("record names and values must have matching lengths");
    }
    Value::record_type record;
    record.is_tuple = is_tuple;
    record.fields = names;
    record.values = values;
    return record;
}

inline bool all_list_values(const std::vector<Value>& values) {
    if (values.empty()) {
        return false;
    }
    for (const auto& value : values) {
        if (value.is_none() || value_tag(value) != ValueTag::list) {
            return false;
        }
    }
    return true;
}

inline Value zip_values(const std::vector<std::string>& names,
                        const std::vector<Value>& values,
                        bool is_tuple,
                        std::optional<int> depth_limit,
                        int depth) {
    if (names.empty()) {
        throw std::invalid_argument("ak::zip requires at least one field");
    }
    if (depth_limit && depth >= *depth_limit) {
        return make_record_value(names, values, is_tuple);
    }
    if (!all_list_values(values)) {
        return make_record_value(names, values, is_tuple);
    }

    const auto row_length = values.front().as_list().size();
    for (const auto& value : values) {
        if (value.as_list().size() != row_length) {
            return make_record_value(names, values, is_tuple);
        }
    }

    Value::list_type result;
    result.reserve(row_length);
    for (std::size_t i = 0; i < row_length; ++i) {
        std::vector<Value> nested_values;
        nested_values.reserve(values.size());
        for (const auto& value : values) {
            nested_values.push_back(value.as_list()[i]);
        }
        result.push_back(zip_values(names, nested_values, is_tuple, depth_limit, depth + 1));
    }
    return result;
}

inline Value with_field_value(const Value& base, const Value& what, const std::string& name) {
    if (base.is_none()) {
        return Value(nullptr);
    }
    if (value_tag(base) == ValueTag::list) {
        if (value_tag(what) != ValueTag::list) {
            throw std::invalid_argument("ak::with_field requires matching nested list structure");
        }
        const auto& base_items = base.as_list();
        const auto& what_items = what.as_list();
        if (base_items.size() != what_items.size()) {
            throw std::invalid_argument("ak::with_field requires matching list lengths");
        }
        Value::list_type result;
        result.reserve(base_items.size());
        for (std::size_t i = 0; i < base_items.size(); ++i) {
            result.push_back(with_field_value(base_items[i], what_items[i], name));
        }
        return result;
    }
    if (value_tag(base) != ValueTag::record) {
        throw std::invalid_argument("ak::with_field requires records");
    }

    auto record = base.as_record();
    if (record.is_tuple) {
        throw std::invalid_argument("ak::with_field requires named records, not tuples");
    }
    const auto found = std::find(record.fields.begin(), record.fields.end(), name);
    if (found == record.fields.end()) {
        record.fields.push_back(name);
        record.values.push_back(what);
    } else {
        const auto index = static_cast<std::size_t>(std::distance(record.fields.begin(), found));
        record.values[index] = what;
    }
    return record;
}

inline Value without_field_value(const Value& base, const std::string& name) {
    if (base.is_none()) {
        return Value(nullptr);
    }
    if (value_tag(base) == ValueTag::list) {
        Value::list_type result;
        result.reserve(base.as_list().size());
        for (const auto& item : base.as_list()) {
            result.push_back(without_field_value(item, name));
        }
        return result;
    }
    if (value_tag(base) != ValueTag::record) {
        throw std::invalid_argument("ak::without_field requires records");
    }

    const auto& source = base.as_record();
    if (source.is_tuple) {
        throw std::invalid_argument("ak::without_field requires named records, not tuples");
    }
    Value::record_type record;
    record.is_tuple = false;
    bool removed = false;
    for (std::size_t i = 0; i < source.fields.size(); ++i) {
        if (source.fields[i] == name) {
            removed = true;
            continue;
        }
        record.fields.push_back(source.fields[i]);
        record.values.push_back(source.values[i]);
    }
    if (!removed) {
        throw std::out_of_range("record field does not exist: " + name);
    }
    return record;
}

inline bool is_flat_scalar_list(const Value& value) {
    if (value_tag(value) != ValueTag::list) return false;
    return std::all_of(value.as_list().begin(), value.as_list().end(), [](const auto& item) {
        return item.is_none() || value_tag(item) != ValueTag::list;
    });
}

inline bool is_nested_list(const Value& value) {
    return value_tag(value) == ValueTag::list &&
           std::any_of(value.as_list().begin(), value.as_list().end(), [](const auto& item) {
               return !item.is_none() && value_tag(item) == ValueTag::list;
           });
}

inline Value broadcast_flat_columns(const Value& flat, const Value& shape) {
    if (shape.is_none()) return Value(nullptr);
    if (value_tag(shape) != ValueTag::list) {
        throw std::invalid_argument("ragged axis-0 broadcast requires nested list shape");
    }
    const auto& columns = flat.as_list();
    const auto& shape_values = shape.as_list();
    const auto deeper = std::any_of(shape_values.begin(), shape_values.end(), [](const auto& item) {
        return !item.is_none() && value_tag(item) == ValueTag::list;
    });
    Value::list_type result;
    result.reserve(shape_values.size());
    if (!deeper) {
        if (shape_values.size() > columns.size()) {
            throw std::invalid_argument("ragged axis-0 broadcast has fewer column values than a row");
        }
        result.insert(result.end(), columns.begin(), columns.begin() + static_cast<std::ptrdiff_t>(shape_values.size()));
        return result;
    }
    for (const auto& item : shape_values) result.push_back(broadcast_flat_columns(flat, item));
    return result;
}

inline std::vector<Value> broadcast_value_list(const std::vector<Value>& values) {
    if (values.empty()) {
        throw std::invalid_argument("ak::broadcast_arrays requires at least one input");
    }

    for (const auto& value : values) {
        if (value.is_none()) {
            return std::vector<Value>(values.size(), Value(nullptr));
        }
    }

    std::optional<std::size_t> list_length;
    for (const auto& value : values) {
        if (value_tag(value) != ValueTag::list) {
            continue;
        }
        const auto size = value.as_list().size();
        if (!list_length) {
            list_length = size;
        } else if (*list_length != size) {
            std::optional<std::size_t> nested_index;
            for (std::size_t i = 0; i < values.size(); ++i) if (is_nested_list(values[i])) nested_index = i;
            if (!nested_index) throw std::invalid_argument("ak::broadcast_arrays requires compatible list lengths");
            std::vector<Value> normalized = values;
            bool changed = false;
            for (std::size_t i = 0; i < normalized.size(); ++i) {
                if (i != *nested_index && is_flat_scalar_list(normalized[i]) &&
                    normalized[i].as_list().size() != values[*nested_index].as_list().size()) {
                    normalized[i] = broadcast_flat_columns(normalized[i], values[*nested_index]);
                    changed = true;
                }
            }
            if (!changed) throw std::invalid_argument("ak::broadcast_arrays requires compatible list lengths");
            return broadcast_value_list(normalized);
        }
    }

    if (!list_length) {
        return values;
    }

    std::vector<Value::list_type> output_lists(values.size());
    for (auto& output : output_lists) {
        output.reserve(*list_length);
    }

    for (std::size_t i = 0; i < *list_length; ++i) {
        std::vector<Value> items;
        items.reserve(values.size());
        for (const auto& value : values) {
            if (value_tag(value) == ValueTag::list) {
                items.push_back(value.as_list()[i]);
            } else {
                items.push_back(value);
            }
        }

        const auto broadcasted = broadcast_value_list(items);
        for (std::size_t output = 0; output < broadcasted.size(); ++output) {
            output_lists[output].push_back(broadcasted[output]);
        }
    }

    std::vector<Value> result;
    result.reserve(values.size());
    for (auto& output : output_lists) {
        result.emplace_back(std::move(output));
    }
    return result;
}

inline bool numeric_equal(const Value& left, const Value& right) {
    return numeric_as_double(left) == numeric_as_double(right);
}

inline Value binary_scalar_value(const Value& left, const Value& right, BinaryOpKind kind) {
    const auto left_tag = value_tag(left);
    const auto right_tag = value_tag(right);

    switch (kind) {
    case BinaryOpKind::add:
    case BinaryOpKind::subtract:
    case BinaryOpKind::multiply: {
        if (!is_numeric_tag(left_tag) || !is_numeric_tag(right_tag)) {
            throw std::invalid_argument("elementwise arithmetic requires numeric values");
        }
        const auto real_result = left_tag == ValueTag::real || right_tag == ValueTag::real;
        const auto left_value = numeric_as_double(left);
        const auto right_value = numeric_as_double(right);
        if (kind == BinaryOpKind::add) {
            return numeric_result(left_value + right_value, real_result);
        }
        if (kind == BinaryOpKind::subtract) {
            return numeric_result(left_value - right_value, real_result);
        }
        return numeric_result(left_value * right_value, real_result);
    }
    case BinaryOpKind::divide:
        if (!is_numeric_tag(left_tag) || !is_numeric_tag(right_tag)) {
            throw std::invalid_argument("elementwise division requires numeric values");
        }
        return numeric_as_double(left) / numeric_as_double(right);
    case BinaryOpKind::equal:
    case BinaryOpKind::not_equal: {
        bool equal = false;
        if (is_numeric_tag(left_tag) && is_numeric_tag(right_tag)) {
            equal = numeric_equal(left, right);
        } else {
            equal = left == right;
        }
        return kind == BinaryOpKind::equal ? equal : !equal;
    }
    case BinaryOpKind::less:
    case BinaryOpKind::less_equal:
    case BinaryOpKind::greater:
    case BinaryOpKind::greater_equal: {
        bool less = false;
        bool greater = false;
        if (is_numeric_tag(left_tag) && is_numeric_tag(right_tag)) {
            const auto left_value = numeric_as_double(left);
            const auto right_value = numeric_as_double(right);
            less = left_value < right_value;
            greater = left_value > right_value;
        } else if (left_tag == ValueTag::string && right_tag == ValueTag::string) {
            const auto& left_value = std::get<std::string>(left.storage());
            const auto& right_value = std::get<std::string>(right.storage());
            less = left_value < right_value;
            greater = left_value > right_value;
        } else {
            throw std::invalid_argument("elementwise comparison requires compatible scalar values");
        }
        if (kind == BinaryOpKind::less) {
            return less;
        }
        if (kind == BinaryOpKind::less_equal) {
            return !greater;
        }
        if (kind == BinaryOpKind::greater) {
            return greater;
        }
        return !less;
    }
    case BinaryOpKind::logical_and:
        return value_truthy(left) && value_truthy(right);
    case BinaryOpKind::logical_or:
        return value_truthy(left) || value_truthy(right);
    }

    throw std::invalid_argument("unsupported elementwise operation");
}

inline void validate_matching_record_fields(const Value::record_type& left, const Value::record_type& right) {
    if (left.is_tuple != right.is_tuple || left.fields.size() != right.fields.size()) {
        throw std::invalid_argument("record broadcasting requires matching fields");
    }
    for (const auto& field : left.fields) {
        if (std::find(right.fields.begin(), right.fields.end(), field) == right.fields.end()) {
            throw std::invalid_argument("record broadcasting requires matching fields");
        }
    }
}

inline std::size_t record_field_index(const Value::record_type& record, const std::string& name) {
    const auto found = std::find(record.fields.begin(), record.fields.end(), name);
    if (found == record.fields.end()) {
        throw std::invalid_argument("record field does not exist: " + name);
    }
    return static_cast<std::size_t>(std::distance(record.fields.begin(), found));
}

inline Value binary_value(const Value& left, const Value& right, BinaryOpKind kind) {
    if (left.is_none() || right.is_none()) {
        return Value(nullptr);
    }

    const auto left_tag = value_tag(left);
    const auto right_tag = value_tag(right);
    if (left_tag == ValueTag::list || right_tag == ValueTag::list) {
        const auto broadcasted = broadcast_value_list({left, right});
        const auto& left_values = broadcasted[0].as_list();
        const auto& right_values = broadcasted[1].as_list();
        Value::list_type result;
        result.reserve(left_values.size());
        for (std::size_t i = 0; i < left_values.size(); ++i) {
            result.push_back(binary_value(left_values[i], right_values[i], kind));
        }
        return result;
    }

    if (left_tag == ValueTag::record || right_tag == ValueTag::record) {
        if (left_tag != ValueTag::record || right_tag != ValueTag::record) {
            throw std::invalid_argument("elementwise record operations require record pairs");
        }
        const auto& left_record = left.as_record();
        const auto& right_record = right.as_record();
        validate_matching_record_fields(left_record, right_record);

        Value::record_type result;
        result.is_tuple = left_record.is_tuple;
        result.fields = left_record.fields;
        result.values.reserve(left_record.values.size());
        for (std::size_t i = 0; i < left_record.fields.size(); ++i) {
            const auto right_index = record_field_index(right_record, left_record.fields[i]);
            result.values.push_back(binary_value(left_record.values[i], right_record.values[right_index], kind));
        }
        return result;
    }

    return binary_scalar_value(left, right, kind);
}

inline Value logical_not_value(const Value& value) {
    if (value.is_none()) {
        return Value(nullptr);
    }
    if (value_tag(value) == ValueTag::list) {
        Value::list_type result;
        result.reserve(value.as_list().size());
        for (const auto& item : value.as_list()) {
            result.push_back(logical_not_value(item));
        }
        return result;
    }
    return !value_truthy(value);
}

inline Value where_value(const Value& condition, const Value& left, const Value& right) {
    if (condition.is_none()) {
        return Value(nullptr);
    }
    if (value_tag(condition) == ValueTag::list || value_tag(left) == ValueTag::list || value_tag(right) == ValueTag::list) {
        const auto broadcasted = broadcast_value_list({condition, left, right});
        const auto& conditions = broadcasted[0].as_list();
        const auto& left_values = broadcasted[1].as_list();
        const auto& right_values = broadcasted[2].as_list();
        Value::list_type result;
        result.reserve(conditions.size());
        for (std::size_t i = 0; i < conditions.size(); ++i) {
            result.push_back(where_value(conditions[i], left_values[i], right_values[i]));
        }
        return result;
    }
    if (value_tag(condition) != ValueTag::boolean) {
        throw std::invalid_argument("ak::where requires boolean conditions");
    }
    return std::get<bool>(condition.storage()) ? left : right;
}

inline Value isclose_value(const Value& left, const Value& right, double rtol, double atol, bool equal_nan) {
    if (left.is_none() || right.is_none()) {
        return Value(nullptr);
    }
    const auto left_tag = value_tag(left);
    const auto right_tag = value_tag(right);
    if (left_tag == ValueTag::list || right_tag == ValueTag::list) {
        const auto broadcasted = broadcast_value_list({left, right});
        const auto& left_values = broadcasted[0].as_list();
        const auto& right_values = broadcasted[1].as_list();
        Value::list_type result;
        result.reserve(left_values.size());
        for (std::size_t i = 0; i < left_values.size(); ++i) {
            result.push_back(isclose_value(left_values[i], right_values[i], rtol, atol, equal_nan));
        }
        return result;
    }
    if (!is_numeric_tag(left_tag) || !is_numeric_tag(right_tag)) {
        throw std::invalid_argument("ak::isclose requires numeric values");
    }
    const auto left_value = numeric_as_double(left);
    const auto right_value = numeric_as_double(right);
    if (std::isnan(left_value) || std::isnan(right_value)) {
        return equal_nan && std::isnan(left_value) && std::isnan(right_value);
    }
    return std::fabs(left_value - right_value) <= (atol + rtol * std::fabs(right_value));
}

inline bool equal_value(const Value& left, const Value& right, bool equal_nan) {
    const auto left_tag = value_tag(left);
    const auto right_tag = value_tag(right);
    if (left.is_none() || right.is_none()) {
        return left.is_none() && right.is_none();
    }
    if (left_tag == ValueTag::list || right_tag == ValueTag::list) {
        if (left_tag != ValueTag::list || right_tag != ValueTag::list) {
            return false;
        }
        const auto& left_values = left.as_list();
        const auto& right_values = right.as_list();
        if (left_values.size() != right_values.size()) {
            return false;
        }
        for (std::size_t i = 0; i < left_values.size(); ++i) {
            if (!equal_value(left_values[i], right_values[i], equal_nan)) {
                return false;
            }
        }
        return true;
    }
    if (left_tag == ValueTag::record || right_tag == ValueTag::record) {
        if (left_tag != ValueTag::record || right_tag != ValueTag::record) {
            return false;
        }
        const auto& left_record = left.as_record();
        const auto& right_record = right.as_record();
        if (left_record.is_tuple != right_record.is_tuple || left_record.fields != right_record.fields ||
            left_record.values.size() != right_record.values.size()) {
            return false;
        }
        for (std::size_t i = 0; i < left_record.values.size(); ++i) {
            if (!equal_value(left_record.values[i], right_record.values[i], equal_nan)) {
                return false;
            }
        }
        return true;
    }
    if (is_numeric_tag(left_tag) && is_numeric_tag(right_tag)) {
        const auto left_value = numeric_as_double(left);
        const auto right_value = numeric_as_double(right);
        if (std::isnan(left_value) || std::isnan(right_value)) {
            return equal_nan && std::isnan(left_value) && std::isnan(right_value);
        }
        return left_value == right_value;
    }
    return left == right;
}

inline bool almost_equal_value(const Value& left, const Value& right, double rtol, double atol, bool equal_nan) {
    const auto left_tag = value_tag(left);
    const auto right_tag = value_tag(right);
    if (left.is_none() || right.is_none()) {
        return left.is_none() && right.is_none();
    }
    if (left_tag == ValueTag::list || right_tag == ValueTag::list) {
        if (left_tag != ValueTag::list || right_tag != ValueTag::list) {
            return false;
        }
        const auto& left_values = left.as_list();
        const auto& right_values = right.as_list();
        if (left_values.size() != right_values.size()) {
            return false;
        }
        for (std::size_t i = 0; i < left_values.size(); ++i) {
            if (!almost_equal_value(left_values[i], right_values[i], rtol, atol, equal_nan)) {
                return false;
            }
        }
        return true;
    }
    if (left_tag == ValueTag::record || right_tag == ValueTag::record) {
        if (left_tag != ValueTag::record || right_tag != ValueTag::record) {
            return false;
        }
        const auto& left_record = left.as_record();
        const auto& right_record = right.as_record();
        if (left_record.is_tuple != right_record.is_tuple || left_record.fields != right_record.fields ||
            left_record.values.size() != right_record.values.size()) {
            return false;
        }
        for (std::size_t i = 0; i < left_record.values.size(); ++i) {
            if (!almost_equal_value(left_record.values[i], right_record.values[i], rtol, atol, equal_nan)) {
                return false;
            }
        }
        return true;
    }
    if (is_numeric_tag(left_tag) && is_numeric_tag(right_tag)) {
        const auto left_value = numeric_as_double(left);
        const auto right_value = numeric_as_double(right);
        if (std::isnan(left_value) || std::isnan(right_value)) {
            return equal_nan && std::isnan(left_value) && std::isnan(right_value);
        }
        return std::fabs(left_value - right_value) <= (atol + rtol * std::fabs(right_value));
    }
    return left == right;
}

inline Value like_value(const Value& value, const Value& fill) {
    if (value.is_none()) {
        return Value(nullptr);
    }
    const auto tag = value_tag(value);
    if (tag == ValueTag::list) {
        Value::list_type result;
        result.reserve(value.as_list().size());
        for (const auto& item : value.as_list()) {
            result.push_back(like_value(item, fill));
        }
        return result;
    }
    if (tag == ValueTag::record) {
        const auto& source = value.as_record();
        Value::record_type result;
        result.is_tuple = source.is_tuple;
        result.fields = source.fields;
        result.values.reserve(source.values.size());
        for (const auto& item : source.values) {
            result.values.push_back(like_value(item, fill));
        }
        return result;
    }
    return fill;
}

inline Value zero_for_value(const Value& value) {
    const auto tag = value_tag(value);
    if (tag == ValueTag::real) {
        return 0.0;
    }
    if (tag == ValueTag::boolean) {
        return false;
    }
    if (tag == ValueTag::integer) {
        return std::int64_t{0};
    }
    throw std::invalid_argument("ak::zeros_like requires numeric or boolean values");
}

inline Value one_for_value(const Value& value) {
    const auto tag = value_tag(value);
    if (tag == ValueTag::real) {
        return 1.0;
    }
    if (tag == ValueTag::boolean) {
        return true;
    }
    if (tag == ValueTag::integer) {
        return std::int64_t{1};
    }
    throw std::invalid_argument("ak::ones_like requires numeric or boolean values");
}

inline Value zeros_like_value(const Value& value) {
    if (value.is_none()) {
        return Value(nullptr);
    }
    if (value_tag(value) == ValueTag::list) {
        Value::list_type result;
        result.reserve(value.as_list().size());
        for (const auto& item : value.as_list()) {
            result.push_back(zeros_like_value(item));
        }
        return result;
    }
    if (value_tag(value) == ValueTag::record) {
        const auto& source = value.as_record();
        Value::record_type result;
        result.is_tuple = source.is_tuple;
        result.fields = source.fields;
        result.values.reserve(source.values.size());
        for (const auto& item : source.values) {
            result.values.push_back(zeros_like_value(item));
        }
        return result;
    }
    return zero_for_value(value);
}

inline Value ones_like_value(const Value& value) {
    if (value.is_none()) {
        return Value(nullptr);
    }
    if (value_tag(value) == ValueTag::list) {
        Value::list_type result;
        result.reserve(value.as_list().size());
        for (const auto& item : value.as_list()) {
            result.push_back(ones_like_value(item));
        }
        return result;
    }
    if (value_tag(value) == ValueTag::record) {
        const auto& source = value.as_record();
        Value::record_type result;
        result.is_tuple = source.is_tuple;
        result.fields = source.fields;
        result.values.reserve(source.values.size());
        for (const auto& item : source.values) {
            result.values.push_back(ones_like_value(item));
        }
        return result;
    }
    return one_for_value(value);
}

inline std::pair<Value, Value> broadcast_fields_value(const Value& left, const Value& right) {
    if (left.is_none() || right.is_none()) {
        return {Value(nullptr), Value(nullptr)};
    }
    const auto left_tag = value_tag(left);
    const auto right_tag = value_tag(right);
    if (left_tag == ValueTag::list || right_tag == ValueTag::list) {
        const auto broadcasted = broadcast_value_list({left, right});
        const auto& left_values = broadcasted[0].as_list();
        const auto& right_values = broadcasted[1].as_list();
        Value::list_type left_result;
        Value::list_type right_result;
        left_result.reserve(left_values.size());
        right_result.reserve(right_values.size());
        for (std::size_t i = 0; i < left_values.size(); ++i) {
            auto nested = broadcast_fields_value(left_values[i], right_values[i]);
            left_result.push_back(std::move(nested.first));
            right_result.push_back(std::move(nested.second));
        }
        return {Value(std::move(left_result)), Value(std::move(right_result))};
    }
    if (left_tag != ValueTag::record || right_tag != ValueTag::record) {
        return {left, right};
    }

    const auto& left_record = left.as_record();
    const auto& right_record = right.as_record();
    validate_matching_record_fields(left_record, right_record);

    Value::record_type left_result;
    Value::record_type right_result;
    left_result.is_tuple = left_record.is_tuple;
    right_result.is_tuple = left_record.is_tuple;
    left_result.fields = left_record.fields;
    right_result.fields = left_record.fields;
    left_result.values.reserve(left_record.values.size());
    right_result.values.reserve(left_record.values.size());

    for (std::size_t i = 0; i < left_record.fields.size(); ++i) {
        const auto right_index = record_field_index(right_record, left_record.fields[i]);
        auto nested = broadcast_fields_value(left_record.values[i], right_record.values[right_index]);
        left_result.values.push_back(std::move(nested.first));
        right_result.values.push_back(std::move(nested.second));
    }
    return {Value(std::move(left_result)), Value(std::move(right_result))};
}

template <typename T>
const std::vector<T>& require_buffer(const BufferMap& buffers, const std::string& key) {
    const auto found = buffers.find(key);
    if (found == buffers.end()) {
        throw std::invalid_argument("missing buffer: " + key);
    }
    const auto* values = std::get_if<std::vector<T>>(&found->second);
    if (values == nullptr) {
        throw std::invalid_argument("buffer has unexpected type: " + key);
    }
    return *values;
}

inline std::vector<std::size_t> offsets_from_buffer(const BufferMap& buffers, const std::string& key) {
    const auto& raw = require_buffer<std::int64_t>(buffers, key);
    std::vector<std::size_t> result;
    result.reserve(raw.size());
    for (const auto value : raw) {
        if (value < 0) {
            throw std::invalid_argument("offset buffers must not contain negative values");
        }
        result.push_back(static_cast<std::size_t>(value));
    }
    return result;
}

inline std::vector<std::ptrdiff_t> signed_index_from_buffer(const BufferMap& buffers, const std::string& key) {
    const auto& raw = require_buffer<std::int64_t>(buffers, key);
    std::vector<std::ptrdiff_t> result;
    result.reserve(raw.size());
    for (const auto value : raw) {
        result.push_back(static_cast<std::ptrdiff_t>(value));
    }
    return result;
}

inline void require_form_length(const Form& form, std::size_t length) {
    if (form.length != length) {
        throw std::invalid_argument("form length does not match requested array length");
    }
}

inline const Form& require_single_content(const Form& form) {
    if (form.contents.size() != 1) {
        throw std::invalid_argument("form requires exactly one content");
    }
    return form.contents.front();
}

template <typename T>
std::shared_ptr<const Content> list_offset_from_typed_content(const std::shared_ptr<const Content>& content,
                                                             std::vector<std::size_t> offsets) {
    const auto typed = std::dynamic_pointer_cast<const NumpyArray<T>>(content);
    if (!typed) {
        return nullptr;
    }
    return std::make_shared<ListOffsetArray<T>>(typed, std::move(offsets));
}

template <typename T>
std::shared_ptr<const Content> regular_from_typed_content(const std::shared_ptr<const Content>& content,
                                                          std::size_t size,
                                                          std::size_t length) {
    const auto typed = std::dynamic_pointer_cast<const NumpyArray<T>>(content);
    if (!typed) {
        return nullptr;
    }
    return std::make_shared<RegularArray<T>>(typed, size, length);
}

inline std::shared_ptr<const Content> primitive_content_from_buffers(const Form& form, const BufferMap& buffers) {
    const auto key = form.key + "-data";
    if (form.primitive == "bool") {
        const auto values = require_buffer<bool>(buffers, key);
        if (values.size() != form.length) {
            throw std::invalid_argument("primitive buffer length does not match form length");
        }
        return std::make_shared<NumpyArray<bool>>(values);
    }
    if (form.primitive == "int64") {
        const auto values = require_buffer<std::int64_t>(buffers, key);
        if (values.size() != form.length) {
            throw std::invalid_argument("primitive buffer length does not match form length");
        }
        return std::make_shared<NumpyArray<std::int64_t>>(values);
    }
    if (form.primitive == "uint64") {
        const auto values = require_buffer<std::uint64_t>(buffers, key);
        if (values.size() != form.length) {
            throw std::invalid_argument("primitive buffer length does not match form length");
        }
        return std::make_shared<NumpyArray<std::uint64_t>>(values);
    }
    if (form.primitive == "float32") {
        const auto values = require_buffer<float>(buffers, key);
        if (values.size() != form.length) {
            throw std::invalid_argument("primitive buffer length does not match form length");
        }
        return std::make_shared<NumpyArray<float>>(values);
    }
    if (form.primitive == "float64") {
        const auto values = require_buffer<double>(buffers, key);
        if (values.size() != form.length) {
            throw std::invalid_argument("primitive buffer length does not match form length");
        }
        return std::make_shared<NumpyArray<double>>(values);
    }
    if (form.primitive == "uint8") {
        const auto values = require_buffer<std::uint8_t>(buffers, key);
        if (values.size() != form.length) {
            throw std::invalid_argument("primitive buffer length does not match form length");
        }
        return std::make_shared<NumpyArray<std::uint8_t>>(values);
    }
    if (form.primitive == "string") {
        const auto values = require_buffer<std::string>(buffers, key);
        if (values.size() != form.length) {
            throw std::invalid_argument("primitive buffer length does not match form length");
        }
        return std::make_shared<NumpyArray<std::string>>(values);
    }
    throw std::invalid_argument("unsupported primitive form: " + form.primitive);
}

inline std::shared_ptr<const Content> content_from_buffers(const Form& form,
                                                           std::size_t length,
                                                           const BufferMap& buffers) {
    require_form_length(form, length);

    switch (form.kind) {
    case FormKind::empty:
        if (length != 0) {
            throw std::invalid_argument("empty form length must be zero");
        }
        return std::make_shared<EmptyArray>();
    case FormKind::numpy:
        return primitive_content_from_buffers(form, buffers);
    case FormKind::list: {
        const auto& content_form = require_single_content(form);
        auto starts = offsets_from_buffer(buffers, form.key + "-starts");
        auto stops = offsets_from_buffer(buffers, form.key + "-stops");
        if (starts.size() != length || stops.size() != length) {
            throw std::invalid_argument("list starts and stops lengths must match array length");
        }
        return std::make_shared<ListArray>(
            std::move(starts), std::move(stops),
            content_from_buffers(content_form, content_form.length, buffers));
    }
    case FormKind::list_offset: {
        const auto& content_form = require_single_content(form);
        auto offsets = offsets_from_buffer(buffers, form.key + "-offsets");
        if (offsets.size() != length + 1) {
            throw std::invalid_argument("list-offset buffer length must equal array length plus one");
        }
        if (offsets.empty() || offsets.front() != 0 || !std::is_sorted(offsets.begin(), offsets.end())) {
            throw std::invalid_argument("list-offset buffer must start at zero and be monotonic");
        }
        const auto string_parameter = form.parameters.find("__array__");
        if (string_parameter != form.parameters.end() && string_parameter->second == "string") {
            if (content_form.kind != FormKind::numpy || content_form.primitive != "uint8") {
                throw std::invalid_argument("string form requires uint8 primitive content");
            }
            const auto bytes = require_buffer<std::uint8_t>(buffers, content_form.key + "-data");
            if (bytes.size() != content_form.length || offsets.back() != bytes.size()) {
                throw std::invalid_argument("string byte buffer length does not match string offsets");
            }
            return std::make_shared<StringArray>(bytes, std::move(offsets));
        }
        auto content = content_from_buffers(content_form, offsets.back(), buffers);
        if (auto layout = list_offset_from_typed_content<bool>(content, offsets)) {
            return layout;
        }
        if (auto layout = list_offset_from_typed_content<std::int64_t>(content, offsets)) {
            return layout;
        }
        if (auto layout = list_offset_from_typed_content<std::uint64_t>(content, offsets)) {
            return layout;
        }
        if (auto layout = list_offset_from_typed_content<float>(content, offsets)) {
            return layout;
        }
        if (auto layout = list_offset_from_typed_content<double>(content, offsets)) {
            return layout;
        }
        if (auto layout = list_offset_from_typed_content<std::string>(content, offsets)) {
            return layout;
        }
        return std::make_shared<ListOffsetContentArray>(content, std::move(offsets));
    }
    case FormKind::regular: {
        const auto& content_form = require_single_content(form);
        const auto content_length = form.size == 0 ? 0 : length * form.size;
        auto content = content_from_buffers(content_form, content_length, buffers);
        if (auto layout = regular_from_typed_content<bool>(content, form.size, length)) {
            return layout;
        }
        if (auto layout = regular_from_typed_content<std::int64_t>(content, form.size, length)) {
            return layout;
        }
        if (auto layout = regular_from_typed_content<std::uint64_t>(content, form.size, length)) {
            return layout;
        }
        if (auto layout = regular_from_typed_content<float>(content, form.size, length)) {
            return layout;
        }
        if (auto layout = regular_from_typed_content<double>(content, form.size, length)) {
            return layout;
        }
        if (const auto strings = std::dynamic_pointer_cast<const StringArray>(content)) {
            return std::make_shared<RegularArray<std::string>>(strings->strings(), form.size, length);
        }
        if (auto layout = regular_from_typed_content<std::string>(content, form.size, length)) {
            return layout;
        }
        return std::make_shared<RegularContentArray>(std::move(content), form.size, length);
    }
    case FormKind::indexed: {
        const auto& content_form = require_single_content(form);
        auto index = signed_index_from_buffer(buffers, form.key + "-index");
        if (index.size() != length) {
            throw std::invalid_argument("indexed array index length must match array length");
        }
        return std::make_shared<IndexedArray>(
            std::move(index), content_from_buffers(content_form, content_form.length, buffers));
    }
    case FormKind::indexed_option: {
        const auto& content_form = require_single_content(form);
        auto index = signed_index_from_buffer(buffers, form.key + "-index");
        if (index.size() != length) {
            throw std::invalid_argument("indexed-option index length must match array length");
        }
        return std::make_shared<IndexedOptionArray>(
            std::move(index), content_from_buffers(content_form, content_form.length, buffers));
    }
    case FormKind::byte_masked: {
        const auto& content_form = require_single_content(form);
        auto mask = require_buffer<std::uint8_t>(buffers, form.key + "-mask");
        if (mask.size() != length) {
            throw std::invalid_argument("byte-mask length must match array length");
        }
        return std::make_shared<ByteMaskedArray>(
            mask, content_from_buffers(content_form, content_form.length, buffers), form.valid_when);
    }
    case FormKind::bit_masked: {
        const auto& content_form = require_single_content(form);
        auto mask = require_buffer<std::uint8_t>(buffers, form.key + "-mask");
        if (mask.size() * 8 < length) {
            throw std::invalid_argument("bit-mask buffer does not contain enough bits for array length");
        }
        return std::make_shared<BitMaskedArray>(
            mask, content_from_buffers(content_form, content_form.length, buffers), length, form.valid_when,
            form.lsb_order);
    }
    case FormKind::unmasked: {
        const auto& content_form = require_single_content(form);
        return std::make_shared<UnmaskedArray>(content_from_buffers(content_form, length, buffers));
    }
    case FormKind::record: {
        if (form.fields.size() != form.contents.size()) {
            throw std::invalid_argument("record form fields and contents must have matching sizes");
        }
        std::vector<std::shared_ptr<const Content>> contents;
        contents.reserve(form.contents.size());
        for (const auto& content_form : form.contents) {
            contents.push_back(content_from_buffers(content_form, length, buffers));
        }
        return std::make_shared<RecordArray>(form.fields, std::move(contents), form.is_tuple, form.record_name, length);
    }
    case FormKind::union_: {
        auto tags = require_buffer<std::uint8_t>(buffers, form.key + "-tags");
        auto index = signed_index_from_buffer(buffers, form.key + "-index");
        if (tags.size() != length || index.size() != length) {
            throw std::invalid_argument("union tags and index lengths must match array length");
        }
        std::vector<std::shared_ptr<const Content>> contents;
        contents.reserve(form.contents.size());
        for (const auto& content_form : form.contents) {
            contents.push_back(content_from_buffers(content_form, content_form.length, buffers));
        }
        return std::make_shared<UnionArray>(std::move(tags), std::move(index), std::move(contents));
    }
    }

    throw std::invalid_argument("unsupported form kind");
}

}  // namespace detail

namespace detail {

inline Array attach_metadata(Array result, const Array& source, bool preserve_named_axes = true) {
    result = result.with_behavior(source.behavior()).with_attrs(source.attrs());
    return result.with_named_axes(preserve_named_axes ? source.named_axes() : Array::NamedAxes{});
}

inline Array attach_metadata(std::shared_ptr<const Content> layout,
                             const Array& source,
                             bool preserve_named_axes = true) {
    return attach_metadata(Array(std::move(layout)), source, preserve_named_axes);
}

template <typename Map>
Map merge_metadata_map(const Map& left, const Map& right, const char* label) {
    Map result = left;
    for (const auto& [key, value] : right) {
        const auto found = result.find(key);
        if (found != result.end() && found->second != value) {
            throw std::invalid_argument(std::string("conflicting array ") + label + ": " + key);
        }
        result[key] = value;
    }
    return result;
}

inline Array attach_merged_metadata(Array result, const Array& left, const Array& right) {
    return result
        .with_behavior(merge_metadata_map(left.behavior(), right.behavior(), "behavior"))
        .with_attrs(merge_metadata_map(left.attrs(), right.attrs(), "attrs"))
        .with_named_axes(merge_metadata_map(left.named_axes(), right.named_axes(), "named axes"));
}

inline Array remove_axis_metadata(Array result, const Array& source, int removed_axis) {
    Array::NamedAxes axes;
    for (const auto& [name, axis] : source.named_axes()) {
        auto normalized = axis;
        if (normalized < 0) normalized += static_cast<int>(source.ndim());
        if (normalized == removed_axis) continue;
        if (normalized > removed_axis) --normalized;
        axes[name] = normalized;
    }
    return result.with_behavior(source.behavior()).with_attrs(source.attrs()).with_named_axes(std::move(axes));
}

inline Array metadata_from_inputs(Array result, const std::vector<Array>& arrays) {
    if (arrays.empty()) return result;
    auto behavior = arrays.front().behavior();
    auto attrs = arrays.front().attrs();
    auto named_axes = arrays.front().named_axes();
    for (std::size_t i = 1; i < arrays.size(); ++i) {
        behavior = merge_metadata_map(behavior, arrays[i].behavior(), "behavior");
        attrs = merge_metadata_map(attrs, arrays[i].attrs(), "attrs");
        named_axes = merge_metadata_map(named_axes, arrays[i].named_axes(), "named axes");
    }
    return result.with_behavior(std::move(behavior))
        .with_attrs(std::move(attrs))
        .with_named_axes(std::move(named_axes));
}

}  // namespace detail

inline Value to_list(const Array& array) {
    return array.to_list();
}

inline ArrayType type(const Array& array) {
    return array.type();
}

inline ScalarType type(const Scalar& scalar) {
    return scalar.type();
}

inline Scalar scalar(const Value& value) {
    return Scalar(value);
}

inline Array with_attrs(const Array& array, Array::Metadata attrs) {
    return array.with_attrs(std::move(attrs));
}

inline Array with_named_axis(const Array& array, std::string name, int axis) {
    auto axes = array.named_axes();
    axes[std::move(name)] = axis;
    return array.with_named_axes(std::move(axes));
}

inline Array without_named_axis(const Array& array, const std::string& name) {
    auto axes = array.named_axes();
    axes.erase(name);
    return array.with_named_axes(std::move(axes));
}

inline std::string validity_error(const Array& array) {
    return array.validity_error();
}

inline bool is_valid(const Array& array) {
    return array.is_valid();
}

inline ToBuffersResult to_buffers(const Array& array) {
    const auto packed = array.layout().to_packed();
    detail::BufferBuilder builder;
    auto form = packed->to_buffers(builder);
    return ToBuffersResult{
        .form = std::move(form),
        .length = packed->length(),
        .buffers = std::move(builder).release(),
    };
}

inline Array from_buffers(const Form& form, std::size_t length, const BufferMap& buffers) {
    return Array(detail::content_from_buffers(form, length, buffers));
}

inline Array from_buffers(const ToBuffersResult& buffers) {
    return from_buffers(buffers.form, buffers.length, buffers.buffers);
}

inline std::vector<std::size_t> num(const Array& array) {
    return array.layout().num();
}

inline Value num(const Array& array, int axis) {
    const auto depth = static_cast<int>(array.ndim());
    if (axis < 0) axis += depth;
    if (axis < 0 || axis >= depth) throw std::invalid_argument("ak::num axis is outside the array depth");
    return detail::num_at_axis(array.to_list(), axis);
}

inline Array flatten(const Array& array) {
    return detail::remove_axis_metadata(Array(array.layout().flatten()), array, 1);
}

inline Array flatten(const Array& array, int axis) {
    const auto depth = static_cast<int>(array.ndim());
    if (axis < 0) axis += depth;
    if (axis <= 0 || axis >= depth) throw std::invalid_argument("ak::flatten axis must select a nested list");
    return detail::remove_axis_metadata(
        detail::array_from_list(detail::flatten_at_axis(array.to_list(), axis).as_list()), array, axis);
}

inline Array ravel(const Array& array) {
    Value::list_type result;
    detail::collect_ravel_values(array.to_list(), result);
    return detail::attach_metadata(detail::array_from_list(std::move(result)), array, false);
}

inline Array unflatten(const Array& array, std::span<const std::size_t> counts) {
    return detail::attach_metadata(
        Array(array.layout().unflatten(std::vector<std::size_t>(counts.begin(), counts.end()))), array);
}

inline Array unflatten(const Array& array, const std::vector<std::size_t>& counts) {
    return detail::attach_metadata(Array(array.layout().unflatten(counts)), array);
}

inline Array to_packed(const Array& array) {
    return array.with_layout(array.layout().to_packed());
}

inline Array concatenate(const std::vector<Array>& arrays, int axis = 0) {
    if (arrays.empty()) {
        return Array();
    }
    int depth = -1;
    for (const auto& array : arrays) {
        const auto current_depth = static_cast<int>(array.ndim());
        if (depth < 0) {
            depth = current_depth;
        } else if (depth != current_depth) {
            throw std::invalid_argument("ak::concatenate requires arrays with matching depth");
        }
    }
    if (axis < 0) axis += depth;
    if (axis < 0 || axis >= depth) {
        throw std::invalid_argument("ak::concatenate axis is outside the array depth");
    }
    if (axis == 0) {
        if (auto layout = kernel::concatenate_axis0(arrays)) {
            return detail::metadata_from_inputs(Array(std::move(layout)), arrays);
        }
    }
    std::vector<Value> values;
    values.reserve(arrays.size());
    for (const auto& array : arrays) values.push_back(array.to_list());
    return detail::metadata_from_inputs(
        detail::array_from_list(detail::concatenate_values(values, axis).as_list()), arrays);
}

inline Array concatenate(std::initializer_list<Array> arrays, int axis = 0) {
    return concatenate(std::vector<Array>(arrays), axis);
}

inline Array local_index(const Array& array, int axis) {
    const auto depth = static_cast<int>(array.ndim());
    if (axis < 0) axis += depth;
    if (axis < 0 || axis >= depth) {
        throw std::invalid_argument("ak::local_index axis is outside the array depth");
    }
    return detail::attach_metadata(
        detail::array_from_list(detail::local_index_at_axis(array.to_list(), axis).as_list()), array);
}

inline Array local_index(const Array& array) {
    return local_index(array, -1);
}

inline Array is_none(const Array& array, int axis = 0) {
    const auto depth = static_cast<int>(array.ndim());
    if (axis < 0) axis += depth;
    if (axis < 0 || axis >= depth) throw std::invalid_argument("ak::is_none axis is outside the array depth");
    auto values = detail::require_top_list(array);
    Value::list_type result;
    result.reserve(values.size());
    for (const auto& value : values) {
        result.push_back(detail::is_none_value(value, axis, 0));
    }
    return detail::attach_metadata(detail::array_from_list(std::move(result)), array);
}

inline Array drop_none(const Array& array, std::optional<int> axis = std::nullopt) {
    if (axis) {
        const auto depth = static_cast<int>(array.ndim());
        if (*axis < 0) *axis += depth;
        if (*axis < 0 || *axis >= depth) {
            throw std::invalid_argument("ak::drop_none axis is outside the array depth");
        }
    }
    auto values = detail::require_top_list(array);
    Value::list_type result;
    result.reserve(values.size());
    for (const auto& value : values) {
        if (!detail::should_drop_at_axis(value, axis, 0)) {
            result.push_back(detail::drop_none_value(value, axis, 0));
        }
    }
    return detail::attach_metadata(detail::array_from_list(std::move(result)), array);
}

inline Array fill_none(const Array& array, const Value& fill_value) {
    auto values = detail::require_top_list(array);
    Value::list_type result;
    result.reserve(values.size());
    for (const auto& value : values) {
        result.push_back(detail::fill_none_value(value, fill_value));
    }
    return detail::attach_metadata(detail::array_from_list(std::move(result)), array);
}

struct PadNoneOptions {
    int axis{1};
    bool clip{false};
};

inline Array pad_none(const Array& array, std::size_t target, PadNoneOptions options = {}) {
    const auto depth = static_cast<int>(array.ndim());
    if (options.axis < 0) options.axis += depth;
    if (options.axis < 0 || options.axis >= depth) {
        throw std::invalid_argument("ak::pad_none axis is outside the array depth");
    }
    const auto value = array.to_list();
    return detail::attach_metadata(
        Array(detail::layout_from_list(detail::pad_none_value(value, target, options.axis, options.clip, 0).as_list())),
        array);
}

inline Array nan_to_none(const Array& array) {
    const auto value = detail::nan_to_none_value(array.to_list());
    return detail::attach_metadata(Array(detail::layout_from_list(value.as_list())), array);
}

struct NanToNumOptions {
    double nan{0.0};
    double posinf{std::numeric_limits<double>::max()};
    double neginf{std::numeric_limits<double>::lowest()};
};

inline Array nan_to_num(const Array& array, NanToNumOptions options = {}) {
    const auto value = detail::nan_to_num_value(array.to_list(), options.nan, options.posinf, options.neginf);
    return detail::attach_metadata(Array(detail::layout_from_list(value.as_list())), array);
}

inline Array mask(const Array& array, const Array& mask, bool valid_when = true) {
    if (const auto* flat_mask = mask.layout().flat_bool_mask()) {
        if (flat_mask->size() != array.length()) {
            throw std::invalid_argument("ak::mask requires mask length to match array length");
        }
        std::vector<std::uint8_t> bytes;
        bytes.reserve(flat_mask->size());
        for (const auto item : *flat_mask) {
            bytes.push_back(item ? 1U : 0U);
        }
        return detail::attach_merged_metadata(
            Array(std::make_shared<ByteMaskedArray>(std::move(bytes), array.layout_ptr(), valid_when)), array, mask);
    }
    const auto value = detail::mask_value(array.to_list(), mask.to_list(), valid_when);
    return detail::attach_merged_metadata(Array(detail::layout_from_list(value.as_list())), array, mask);
}

inline Array firsts(const Array& array, int axis = 1) {
    const auto depth = static_cast<int>(array.ndim());
    if (axis < 0) axis += depth;
    if (axis <= 0 || axis >= depth) throw std::invalid_argument("ak::firsts axis is outside a list depth");
    const auto value = detail::firsts_value(array.to_list(), axis, 0);
    return detail::remove_axis_metadata(Array(detail::layout_from_list(value.as_list())), array, axis);
}

inline Array singletons(const Array& array) {
    return detail::attach_metadata(
        Array(detail::layout_from_list(detail::singletons_value(array.to_list()).as_list())), array);
}

struct ReducerOptions {
    std::optional<int> axis{-1};
    bool keepdims{false};
    bool mask_identity{false};
    std::optional<Value> initial{std::nullopt};
};

struct StatisticOptions {
    std::optional<int> axis{-1};
    bool keepdims{false};
    bool mask_identity{false};
    double ddof{0.0};
};

struct SortOptions {
    int axis{-1};
    bool ascending{true};
};

inline Value reduce(const Array& array,
                    detail::ReducerKind kind,
                    ReducerOptions options = {},
                    bool skip_nan = false,
                    double ddof = 0.0,
                    int moment_order = 2) {
    return detail::reduce_array_value(
        array.to_list(), options.axis, options.keepdims,
        detail::ReduceSettings{
            .kind = kind,
            .mask_identity = options.mask_identity,
            .initial = options.initial,
            .skip_nan = skip_nan,
            .ddof = ddof,
            .moment_order = moment_order,
        });
}

inline ReducerResult reducer_result(Value value,
                                    const Array& source,
                                    std::optional<int> axis,
                                    bool keepdims) {
    if (!std::holds_alternative<Value::list_type>(value.storage())) return Scalar(std::move(value));
    auto result = detail::array_from_list(std::move(value.as_list()));
    if (axis && !keepdims) {
        auto normalized = *axis;
        if (normalized < 0) normalized += static_cast<int>(source.ndim());
        result = detail::remove_axis_metadata(std::move(result), source, normalized);
    } else {
        result = detail::attach_metadata(std::move(result), source);
    }
    return result;
}

inline ReducerResult reduce_result(const Array& array,
                                   detail::ReducerKind kind,
                                   ReducerOptions options = {},
                                   bool skip_nan = false,
                                   double ddof = 0.0,
                                   int moment_order = 2) {
    const auto axis = options.axis;
    const auto keepdims = options.keepdims;
    return reducer_result(
        reduce(array, kind, std::move(options), skip_nan, ddof, moment_order), array, axis, keepdims);
}

inline Value count(const Array& array, ReducerOptions options = {}) {
    return reduce(array, detail::ReducerKind::count, std::move(options));
}

inline Value count_nonzero(const Array& array, ReducerOptions options = {}) {
    return reduce(array, detail::ReducerKind::count_nonzero, std::move(options));
}

inline Value sum(const Array& array, ReducerOptions options = {}) {
    return reduce(array, detail::ReducerKind::sum, std::move(options));
}

inline ReducerResult sum_result(const Array& array, ReducerOptions options = {}) {
    return reduce_result(array, detail::ReducerKind::sum, std::move(options));
}

inline Value prod(const Array& array, ReducerOptions options = {}) {
    return reduce(array, detail::ReducerKind::prod, std::move(options));
}

inline Value any(const Array& array, ReducerOptions options = {}) {
    return reduce(array, detail::ReducerKind::any, std::move(options));
}

inline Value all(const Array& array, ReducerOptions options = {}) {
    return reduce(array, detail::ReducerKind::all, std::move(options));
}

inline Value min(const Array& array, ReducerOptions options = {}) {
    return reduce(array, detail::ReducerKind::min, std::move(options));
}

inline Value max(const Array& array, ReducerOptions options = {}) {
    return reduce(array, detail::ReducerKind::max, std::move(options));
}

inline Value argmin(const Array& array, ReducerOptions options = {}) {
    return reduce(array, detail::ReducerKind::argmin, std::move(options));
}

inline Value argmax(const Array& array, ReducerOptions options = {}) {
    return reduce(array, detail::ReducerKind::argmax, std::move(options));
}

inline Value mean(const Array& array, StatisticOptions options = {}) {
    return reduce(
        array, detail::ReducerKind::mean,
        ReducerOptions{
            .axis = options.axis,
            .keepdims = options.keepdims,
            .mask_identity = options.mask_identity,
        });
}

inline ReducerResult mean_result(const Array& array, StatisticOptions options = {}) {
    const auto axis = options.axis;
    const auto keepdims = options.keepdims;
    return reducer_result(mean(array, options), array, axis, keepdims);
}

inline Value moment(const Array& array, int order, StatisticOptions options = {}) {
    if (order < 0) {
        throw std::invalid_argument("ak::moment order must be non-negative");
    }
    return reduce(
        array, detail::ReducerKind::moment,
        ReducerOptions{
            .axis = options.axis,
            .keepdims = options.keepdims,
            .mask_identity = options.mask_identity,
        },
        false, options.ddof, order);
}

inline Value var(const Array& array, StatisticOptions options = {}) {
    return reduce(
        array, detail::ReducerKind::var,
        ReducerOptions{
            .axis = options.axis,
            .keepdims = options.keepdims,
            .mask_identity = options.mask_identity,
        },
        false, options.ddof);
}

inline Value std(const Array& array, StatisticOptions options = {}) {
    return reduce(
        array, detail::ReducerKind::stddev,
        ReducerOptions{
            .axis = options.axis,
            .keepdims = options.keepdims,
            .mask_identity = options.mask_identity,
        },
        false, options.ddof);
}

inline Value ptp(const Array& array, StatisticOptions options = {}) {
    return reduce(
        array, detail::ReducerKind::ptp,
        ReducerOptions{
            .axis = options.axis,
            .keepdims = options.keepdims,
            .mask_identity = options.mask_identity,
        });
}

inline Value nansum(const Array& array, ReducerOptions options = {}) {
    return reduce(array, detail::ReducerKind::sum, std::move(options), true);
}

inline Value nanprod(const Array& array, ReducerOptions options = {}) {
    return reduce(array, detail::ReducerKind::prod, std::move(options), true);
}

inline Value nanmin(const Array& array, ReducerOptions options = {}) {
    return reduce(array, detail::ReducerKind::min, std::move(options), true);
}

inline Value nanmax(const Array& array, ReducerOptions options = {}) {
    return reduce(array, detail::ReducerKind::max, std::move(options), true);
}

inline Value nanmean(const Array& array, StatisticOptions options = {}) {
    return reduce(
        array, detail::ReducerKind::mean,
        ReducerOptions{
            .axis = options.axis,
            .keepdims = options.keepdims,
            .mask_identity = options.mask_identity,
        },
        true);
}

inline Value nanvar(const Array& array, StatisticOptions options = {}) {
    return reduce(
        array, detail::ReducerKind::var,
        ReducerOptions{
            .axis = options.axis,
            .keepdims = options.keepdims,
            .mask_identity = options.mask_identity,
        },
        true, options.ddof);
}

inline Value nanstd(const Array& array, StatisticOptions options = {}) {
    return reduce(
        array, detail::ReducerKind::stddev,
        ReducerOptions{
            .axis = options.axis,
            .keepdims = options.keepdims,
            .mask_identity = options.mask_identity,
        },
        true, options.ddof);
}

inline Value nanargmin(const Array& array, ReducerOptions options = {}) {
    return reduce(array, detail::ReducerKind::argmin, std::move(options), true);
}

inline Value nanargmax(const Array& array, ReducerOptions options = {}) {
    return reduce(array, detail::ReducerKind::argmax, std::move(options), true);
}

inline Array sort(const Array& array, SortOptions options = {}) {
    return detail::attach_metadata(
        Array(detail::layout_from_list(detail::sort_value(array.to_list(), options.axis, options.ascending, false).as_list())),
        array);
}

inline Array argsort(const Array& array, SortOptions options = {}) {
    return detail::attach_metadata(
        Array(detail::layout_from_list(detail::sort_value(array.to_list(), options.axis, options.ascending, true).as_list())),
        array);
}

inline Array softmax(const Array& array, int axis = -1) {
    return detail::attach_metadata(
        Array(detail::layout_from_list(detail::softmax_value(array.to_list(), axis).as_list())), array);
}

inline std::vector<std::string> fields(const Array& array) {
    return array.fields();
}

inline Array field(const Array& array, const std::string& name) {
    return array.field(name);
}

inline Array project_fields(const Array& array, const std::vector<std::string>& names) {
    return array.project_fields(names);
}

struct ZipOptions {
    std::optional<int> depth_limit{std::nullopt};
};

inline Array zip(const std::vector<std::pair<std::string, Array>>& fields, ZipOptions options = {}) {
    if (fields.empty()) {
        throw std::invalid_argument("ak::zip requires at least one field");
    }

    const auto length = fields.front().second.length();
    std::vector<std::string> names;
    std::vector<Value::list_type> values_by_field;
    names.reserve(fields.size());
    values_by_field.reserve(fields.size());

    for (const auto& [name, array] : fields) {
        if (name.empty()) {
            throw std::invalid_argument("ak::zip field names must not be empty");
        }
        if (array.length() != length) {
            throw std::invalid_argument("ak::zip field arrays must have equal lengths");
        }
        names.push_back(name);
        values_by_field.push_back(detail::require_top_list(array));
    }

    Value::list_type rows;
    rows.reserve(length);
    for (std::size_t row = 0; row < length; ++row) {
        std::vector<Value> row_values;
        row_values.reserve(values_by_field.size());
        for (const auto& field_values : values_by_field) {
            row_values.push_back(field_values[row]);
        }
        rows.push_back(detail::zip_values(names, row_values, false, options.depth_limit, 1));
    }
    std::vector<Array> inputs;
    inputs.reserve(fields.size());
    for (const auto& field : fields) inputs.push_back(field.second);
    return detail::metadata_from_inputs(detail::array_from_list(std::move(rows)), inputs);
}

inline Array zip(std::initializer_list<std::pair<std::string, Array>> fields, ZipOptions options = {}) {
    return zip(std::vector<std::pair<std::string, Array>>(fields.begin(), fields.end()), options);
}

inline Array zip(const std::vector<Array>& arrays, ZipOptions options = {}) {
    if (arrays.empty()) {
        throw std::invalid_argument("ak::zip tuple input requires at least one array");
    }
    const auto length = arrays.front().length();
    std::vector<std::string> names;
    std::vector<Value::list_type> values_by_field;
    names.reserve(arrays.size());
    values_by_field.reserve(arrays.size());
    for (std::size_t i = 0; i < arrays.size(); ++i) {
        if (arrays[i].length() != length) {
            throw std::invalid_argument("ak::zip tuple arrays must have equal lengths");
        }
        names.push_back(std::to_string(i));
        values_by_field.push_back(detail::require_top_list(arrays[i]));
    }

    Value::list_type rows;
    rows.reserve(length);
    for (std::size_t row = 0; row < length; ++row) {
        std::vector<Value> row_values;
        row_values.reserve(values_by_field.size());
        for (const auto& field_values : values_by_field) {
            row_values.push_back(field_values[row]);
        }
        rows.push_back(detail::zip_values(names, row_values, true, options.depth_limit, 1));
    }
    return detail::metadata_from_inputs(detail::array_from_list(std::move(rows)), arrays);
}

inline Array zip_no_broadcast(const std::vector<std::pair<std::string, Array>>& fields) {
    return zip(fields, {.depth_limit = 1});
}

inline Array zip_no_broadcast(std::initializer_list<std::pair<std::string, Array>> fields) {
    return zip_no_broadcast(std::vector<std::pair<std::string, Array>>(fields.begin(), fields.end()));
}

inline std::vector<Array> unzip(const Array& array) {
    const auto names = array.fields();
    if (names.empty()) {
        throw std::invalid_argument("ak::unzip requires a record or tuple array");
    }
    std::vector<Array> result;
    result.reserve(names.size());
    for (const auto& name : names) {
        result.push_back(array.field(name));
    }
    return result;
}

inline Array with_field(const Array& array, const Array& what, const std::string& name) {
    if (array.length() != what.length()) {
        throw std::invalid_argument("ak::with_field requires arrays with equal outer lengths");
    }
    const auto base = detail::require_top_list(array);
    const auto values = detail::require_top_list(what);
    Value::list_type result;
    result.reserve(base.size());
    for (std::size_t i = 0; i < base.size(); ++i) {
        result.push_back(detail::with_field_value(base[i], values[i], name));
    }
    return detail::attach_merged_metadata(detail::array_from_list(std::move(result)), array, what);
}

inline Array without_field(const Array& array, const std::string& name) {
    const auto base = detail::require_top_list(array);
    Value::list_type result;
    result.reserve(base.size());
    for (const auto& item : base) {
        result.push_back(detail::without_field_value(item, name));
    }
    return detail::attach_metadata(detail::array_from_list(std::move(result)), array);
}

inline Array with_name(const Array& array, std::string name) {
    return array.with_layout(array.layout().with_name(std::move(name)));
}

inline std::vector<Array> broadcast_arrays(const std::vector<Array>& arrays) {
    if (arrays.empty()) {
        throw std::invalid_argument("ak::broadcast_arrays requires at least one input");
    }
    std::vector<Value> values;
    values.reserve(arrays.size());
    for (const auto& array : arrays) {
        values.push_back(array.to_list());
    }

    const auto broadcasted = detail::broadcast_value_list(values);
    std::vector<Array> result;
    result.reserve(broadcasted.size());
    for (const auto& value : broadcasted) {
        if (detail::value_tag(value) != detail::ValueTag::list) {
            throw std::invalid_argument("ak::broadcast_arrays produced a non-array result");
        }
        result.push_back(detail::array_from_list(value.as_list()));
    }
    for (std::size_t i = 0; i < result.size(); ++i) {
        result[i] = detail::attach_metadata(std::move(result[i]), arrays[i]);
    }
    return result;
}

inline std::pair<Array, Array> broadcast_arrays(const Array& left, const Array& right) {
    auto result = broadcast_arrays(std::vector<Array>{left, right});
    return {std::move(result[0]), std::move(result[1])};
}

inline std::pair<Array, Array> broadcast_arrays(const Array& array, const Value& scalar) {
    const auto broadcasted = detail::broadcast_value_list({array.to_list(), scalar});
    return {detail::attach_metadata(Array(detail::layout_from_list(broadcasted[0].as_list())), array),
            detail::attach_metadata(Array(detail::layout_from_list(broadcasted[1].as_list())), array)};
}

inline std::pair<Array, Array> broadcast_arrays(const Value& scalar, const Array& array) {
    const auto broadcasted = detail::broadcast_value_list({scalar, array.to_list()});
    return {detail::attach_metadata(Array(detail::layout_from_list(broadcasted[0].as_list())), array),
            detail::attach_metadata(Array(detail::layout_from_list(broadcasted[1].as_list())), array)};
}

inline std::pair<Array, Array> broadcast_fields(const Array& left, const Array& right) {
    const auto result = detail::broadcast_fields_value(left.to_list(), right.to_list());
    return {detail::attach_merged_metadata(Array(detail::layout_from_list(result.first.as_list())), left, right),
            detail::attach_merged_metadata(Array(detail::layout_from_list(result.second.as_list())), left, right)};
}

inline Array elementwise_binary(const Array& left, const Array& right, detail::BinaryOpKind kind) {
    if (left.length() == 0 && right.length() == 0 && left.ndim() == right.ndim()) {
        return detail::attach_merged_metadata(left.with_layout(left.layout_ptr()), left, right);
    }
    if (auto layout = kernel::binary(left.layout(), right.layout(), detail::kernel_operation(kind))) {
        return detail::attach_merged_metadata(Array(std::move(layout)), left, right);
    }
    return detail::attach_merged_metadata(
        Array(detail::layout_from_list(detail::binary_value(left.to_list(), right.to_list(), kind).as_list())),
        left, right);
}

inline Array elementwise_binary(const Array& left, const Value& right, detail::BinaryOpKind kind) {
    if (left.length() == 0) return left.with_layout(left.layout_ptr());
    if (auto layout = kernel::binary(left.layout(), right, detail::kernel_operation(kind))) {
        return detail::attach_metadata(Array(std::move(layout)), left);
    }
    return detail::attach_metadata(
        Array(detail::layout_from_list(detail::binary_value(left.to_list(), right, kind).as_list())), left);
}

inline Array elementwise_binary(const Value& left, const Array& right, detail::BinaryOpKind kind) {
    if (right.length() == 0) return right.with_layout(right.layout_ptr());
    if (auto layout = kernel::binary(right.layout(), left, detail::kernel_operation(kind), true)) {
        return detail::attach_metadata(Array(std::move(layout)), right);
    }
    return detail::attach_metadata(
        Array(detail::layout_from_list(detail::binary_value(left, right.to_list(), kind).as_list())), right);
}

inline Array add(const Array& left, const Array& right) {
    return elementwise_binary(left, right, detail::BinaryOpKind::add);
}

inline Array add(const Array& left, const Value& right) {
    return elementwise_binary(left, right, detail::BinaryOpKind::add);
}

inline Array add(const Value& left, const Array& right) {
    return elementwise_binary(left, right, detail::BinaryOpKind::add);
}

inline Array subtract(const Array& left, const Array& right) {
    return elementwise_binary(left, right, detail::BinaryOpKind::subtract);
}

inline Array subtract(const Array& left, const Value& right) {
    return elementwise_binary(left, right, detail::BinaryOpKind::subtract);
}

inline Array subtract(const Value& left, const Array& right) {
    return elementwise_binary(left, right, detail::BinaryOpKind::subtract);
}

inline Array multiply(const Array& left, const Array& right) {
    return elementwise_binary(left, right, detail::BinaryOpKind::multiply);
}

inline Array multiply(const Array& left, const Value& right) {
    return elementwise_binary(left, right, detail::BinaryOpKind::multiply);
}

inline Array multiply(const Value& left, const Array& right) {
    return elementwise_binary(left, right, detail::BinaryOpKind::multiply);
}

inline Array divide(const Array& left, const Array& right) {
    return elementwise_binary(left, right, detail::BinaryOpKind::divide);
}

inline Array divide(const Array& left, const Value& right) {
    return elementwise_binary(left, right, detail::BinaryOpKind::divide);
}

inline Array divide(const Value& left, const Array& right) {
    return elementwise_binary(left, right, detail::BinaryOpKind::divide);
}

inline Array equal(const Array& left, const Array& right) {
    return elementwise_binary(left, right, detail::BinaryOpKind::equal);
}

inline Array equal(const Array& left, const Value& right) {
    return elementwise_binary(left, right, detail::BinaryOpKind::equal);
}

inline Array equal(const Value& left, const Array& right) {
    return elementwise_binary(left, right, detail::BinaryOpKind::equal);
}

inline Array not_equal(const Array& left, const Array& right) {
    return elementwise_binary(left, right, detail::BinaryOpKind::not_equal);
}

inline Array not_equal(const Array& left, const Value& right) {
    return elementwise_binary(left, right, detail::BinaryOpKind::not_equal);
}

inline Array not_equal(const Value& left, const Array& right) {
    return elementwise_binary(left, right, detail::BinaryOpKind::not_equal);
}

inline Array less(const Array& left, const Array& right) {
    return elementwise_binary(left, right, detail::BinaryOpKind::less);
}

inline Array less(const Array& left, const Value& right) {
    return elementwise_binary(left, right, detail::BinaryOpKind::less);
}

inline Array less(const Value& left, const Array& right) {
    return elementwise_binary(left, right, detail::BinaryOpKind::less);
}

inline Array less_equal(const Array& left, const Array& right) {
    return elementwise_binary(left, right, detail::BinaryOpKind::less_equal);
}

inline Array less_equal(const Array& left, const Value& right) {
    return elementwise_binary(left, right, detail::BinaryOpKind::less_equal);
}

inline Array less_equal(const Value& left, const Array& right) {
    return elementwise_binary(left, right, detail::BinaryOpKind::less_equal);
}

inline Array greater(const Array& left, const Array& right) {
    return elementwise_binary(left, right, detail::BinaryOpKind::greater);
}

inline Array greater(const Array& left, const Value& right) {
    return elementwise_binary(left, right, detail::BinaryOpKind::greater);
}

inline Array greater(const Value& left, const Array& right) {
    return elementwise_binary(left, right, detail::BinaryOpKind::greater);
}

inline Array greater_equal(const Array& left, const Array& right) {
    return elementwise_binary(left, right, detail::BinaryOpKind::greater_equal);
}

inline Array greater_equal(const Array& left, const Value& right) {
    return elementwise_binary(left, right, detail::BinaryOpKind::greater_equal);
}

inline Array greater_equal(const Value& left, const Array& right) {
    return elementwise_binary(left, right, detail::BinaryOpKind::greater_equal);
}

inline Array logical_and(const Array& left, const Array& right) {
    return elementwise_binary(left, right, detail::BinaryOpKind::logical_and);
}

inline Array logical_and(const Array& left, const Value& right) {
    return elementwise_binary(left, right, detail::BinaryOpKind::logical_and);
}

inline Array logical_and(const Value& left, const Array& right) {
    return elementwise_binary(left, right, detail::BinaryOpKind::logical_and);
}

inline Array logical_or(const Array& left, const Array& right) {
    return elementwise_binary(left, right, detail::BinaryOpKind::logical_or);
}

inline Array logical_or(const Array& left, const Value& right) {
    return elementwise_binary(left, right, detail::BinaryOpKind::logical_or);
}

inline Array logical_or(const Value& left, const Array& right) {
    return elementwise_binary(left, right, detail::BinaryOpKind::logical_or);
}

inline Array logical_not(const Array& array) {
    if (array.length() == 0) return array.with_layout(array.layout_ptr());
    if (auto layout = kernel::logical_not(array.layout())) {
        return detail::attach_metadata(Array(std::move(layout)), array);
    }
    return detail::attach_metadata(
        Array(detail::layout_from_list(detail::logical_not_value(array.to_list()).as_list())), array);
}

inline Array operator+(const Array& left, const Array& right) { return add(left, right); }
inline Array operator+(const Array& left, const Value& right) { return add(left, right); }
inline Array operator+(const Value& left, const Array& right) { return add(left, right); }
inline Array operator-(const Array& left, const Array& right) { return subtract(left, right); }
inline Array operator-(const Array& left, const Value& right) { return subtract(left, right); }
inline Array operator-(const Value& left, const Array& right) { return subtract(left, right); }
inline Array operator*(const Array& left, const Array& right) { return multiply(left, right); }
inline Array operator*(const Array& left, const Value& right) { return multiply(left, right); }
inline Array operator*(const Value& left, const Array& right) { return multiply(left, right); }
inline Array operator/(const Array& left, const Array& right) { return divide(left, right); }
inline Array operator/(const Array& left, const Value& right) { return divide(left, right); }
inline Array operator/(const Value& left, const Array& right) { return divide(left, right); }
inline Array operator==(const Array& left, const Array& right) { return equal(left, right); }
inline Array operator==(const Array& left, const Value& right) { return equal(left, right); }
inline Array operator==(const Value& left, const Array& right) { return equal(left, right); }
inline Array operator!=(const Array& left, const Array& right) { return not_equal(left, right); }
inline Array operator!=(const Array& left, const Value& right) { return not_equal(left, right); }
inline Array operator!=(const Value& left, const Array& right) { return not_equal(left, right); }
inline Array operator<(const Array& left, const Array& right) { return less(left, right); }
inline Array operator<(const Array& left, const Value& right) { return less(left, right); }
inline Array operator<(const Value& left, const Array& right) { return less(left, right); }
inline Array operator<=(const Array& left, const Array& right) { return less_equal(left, right); }
inline Array operator<=(const Array& left, const Value& right) { return less_equal(left, right); }
inline Array operator<=(const Value& left, const Array& right) { return less_equal(left, right); }
inline Array operator>(const Array& left, const Array& right) { return greater(left, right); }
inline Array operator>(const Array& left, const Value& right) { return greater(left, right); }
inline Array operator>(const Value& left, const Array& right) { return greater(left, right); }
inline Array operator>=(const Array& left, const Array& right) { return greater_equal(left, right); }
inline Array operator>=(const Array& left, const Value& right) { return greater_equal(left, right); }
inline Array operator>=(const Value& left, const Array& right) { return greater_equal(left, right); }
inline Array operator&(const Array& left, const Array& right) { return logical_and(left, right); }
inline Array operator&(const Array& left, const Value& right) { return logical_and(left, right); }
inline Array operator&(const Value& left, const Array& right) { return logical_and(left, right); }
inline Array operator|(const Array& left, const Array& right) { return logical_or(left, right); }
inline Array operator|(const Array& left, const Value& right) { return logical_or(left, right); }
inline Array operator|(const Value& left, const Array& right) { return logical_or(left, right); }
inline Array operator!(const Array& array) { return logical_not(array); }

inline Array where(const Array& condition, const Array& left, const Array& right) {
    return detail::metadata_from_inputs(
        Array(detail::layout_from_list(detail::where_value(condition.to_list(), left.to_list(), right.to_list()).as_list())),
        {condition, left, right});
}

inline Array where(const Array& condition, const Array& left, const Value& right) {
    return detail::metadata_from_inputs(
        Array(detail::layout_from_list(detail::where_value(condition.to_list(), left.to_list(), right).as_list())),
        {condition, left});
}

inline Array where(const Array& condition, const Value& left, const Array& right) {
    return detail::metadata_from_inputs(
        Array(detail::layout_from_list(detail::where_value(condition.to_list(), left, right.to_list()).as_list())),
        {condition, right});
}

inline Array where(const Array& condition, const Value& left, const Value& right) {
    return detail::attach_metadata(
        Array(detail::layout_from_list(detail::where_value(condition.to_list(), left, right).as_list())), condition);
}

struct CloseOptions {
    double rtol{1.0e-5};
    double atol{1.0e-8};
    bool equal_nan{false};
};

inline Array isclose(const Array& left, const Array& right, CloseOptions options = {}) {
    return detail::attach_merged_metadata(
        Array(detail::layout_from_list(
            detail::isclose_value(left.to_list(), right.to_list(), options.rtol, options.atol, options.equal_nan).as_list())),
        left, right);
}

inline Array isclose(const Array& left, const Value& right, CloseOptions options = {}) {
    return detail::attach_metadata(
        Array(detail::layout_from_list(
            detail::isclose_value(left.to_list(), right, options.rtol, options.atol, options.equal_nan).as_list())),
        left);
}

inline Array isclose(const Value& left, const Array& right, CloseOptions options = {}) {
    return detail::attach_metadata(
        Array(detail::layout_from_list(
            detail::isclose_value(left, right.to_list(), options.rtol, options.atol, options.equal_nan).as_list())),
        right);
}

inline bool array_equal(const Array& left, const Array& right, bool equal_nan = false) {
    return detail::equal_value(left.to_list(), right.to_list(), equal_nan);
}

inline bool almost_equal(const Array& left, const Array& right, CloseOptions options = {}) {
    return detail::almost_equal_value(left.to_list(), right.to_list(), options.rtol, options.atol, options.equal_nan);
}

inline Array zeros_like(const Array& array) {
    return detail::attach_metadata(
        Array(detail::layout_from_list(detail::zeros_like_value(array.to_list()).as_list())), array);
}

inline Array ones_like(const Array& array) {
    return detail::attach_metadata(
        Array(detail::layout_from_list(detail::ones_like_value(array.to_list()).as_list())), array);
}

inline Array full_like(const Array& array, const Value& fill_value) {
    return detail::attach_metadata(
        Array(detail::layout_from_list(detail::like_value(array.to_list(), fill_value).as_list())), array);
}

template <typename T>
Array from_iter(const std::vector<T>& values) {
    using Storage = detail::storage_type_t<T>;
    std::vector<Storage> normalized;
    normalized.reserve(values.size());
    for (const auto& value : values) {
        normalized.push_back(detail::normalize_value(value));
    }
    if constexpr (detail::is_string_like_v<T>) {
        return Array(std::make_shared<StringArray>(normalized));
    } else {
        return Array(std::make_shared<NumpyArray<Storage>>(std::move(normalized)));
    }
}

template <typename T>
Array from_iter(std::initializer_list<T> values) {
    using Storage = detail::storage_type_t<T>;
    std::vector<Storage> normalized;
    normalized.reserve(values.size());
    for (const auto& value : values) {
        normalized.push_back(detail::normalize_value(value));
    }
    if constexpr (detail::is_string_like_v<T>) {
        return Array(std::make_shared<StringArray>(normalized));
    } else {
        return Array(std::make_shared<NumpyArray<Storage>>(std::move(normalized)));
    }
}

template <typename T>
Array from_iter(const std::vector<std::optional<T>>& values) {
    Value::list_type list;
    list.reserve(values.size());
    for (const auto& value : values) {
        if (value) {
            list.emplace_back(detail::normalize_value(*value));
        } else {
            list.emplace_back(nullptr);
        }
    }
    return detail::array_from_list(std::move(list));
}

template <typename T>
Array from_iter(std::initializer_list<Option<T>> values) {
    Value::list_type list;
    list.reserve(values.size());
    for (const auto& value : values) {
        if (value.has_value()) {
            list.emplace_back(detail::normalize_value(value.value()));
        } else {
            list.emplace_back(nullptr);
        }
    }
    return detail::array_from_list(std::move(list));
}

template <typename T>
Array from_iter(const std::vector<std::vector<T>>& rows) {
    using Storage = detail::storage_type_t<T>;
    std::vector<Storage> values;
    std::vector<std::size_t> offsets;
    offsets.reserve(rows.size() + 1);
    offsets.push_back(0);

    for (const auto& row : rows) {
        for (const auto& value : row) {
            values.push_back(detail::normalize_value(value));
        }
        offsets.push_back(values.size());
    }

    if constexpr (detail::is_string_like_v<T>) {
        auto content = std::make_shared<StringArray>(values);
        return Array(std::make_shared<ListOffsetContentArray>(std::move(content), std::move(offsets)));
    } else {
        return Array(std::make_shared<ListOffsetArray<Storage>>(std::move(values), std::move(offsets)));
    }
}

template <typename T>
Array from_iter(const std::vector<std::vector<std::optional<T>>>& rows) {
    Value::list_type outer;
    outer.reserve(rows.size());
    for (const auto& row : rows) {
        Value::list_type inner;
        inner.reserve(row.size());
        for (const auto& value : row) {
            if (value) {
                inner.emplace_back(detail::normalize_value(*value));
            } else {
                inner.emplace_back(nullptr);
            }
        }
        outer.emplace_back(std::move(inner));
    }
    return detail::array_from_list(std::move(outer));
}

template <typename T>
Array from_iter(std::initializer_list<std::initializer_list<Option<T>>> rows) {
    Value::list_type outer;
    outer.reserve(rows.size());
    for (const auto& row : rows) {
        Value::list_type inner;
        inner.reserve(row.size());
        for (const auto& value : row) {
            if (value.has_value()) {
                inner.emplace_back(detail::normalize_value(value.value()));
            } else {
                inner.emplace_back(nullptr);
            }
        }
        outer.emplace_back(std::move(inner));
    }
    return detail::array_from_list(std::move(outer));
}

template <typename T>
Array from_iter(std::initializer_list<std::initializer_list<T>> rows) {
    using Storage = detail::storage_type_t<T>;
    std::vector<Storage> values;
    std::vector<std::size_t> offsets;
    offsets.reserve(rows.size() + 1);
    offsets.push_back(0);

    for (const auto& row : rows) {
        for (const auto& value : row) {
            values.push_back(detail::normalize_value(value));
        }
        offsets.push_back(values.size());
    }

    if constexpr (detail::is_string_like_v<T>) {
        auto content = std::make_shared<StringArray>(values);
        return Array(std::make_shared<ListOffsetContentArray>(std::move(content), std::move(offsets)));
    } else {
        return Array(std::make_shared<ListOffsetArray<Storage>>(std::move(values), std::move(offsets)));
    }
}

template <typename T>
Array regular(std::vector<T> values, std::size_t size) {
    using Storage = detail::storage_type_t<T>;
    std::vector<Storage> normalized;
    normalized.reserve(values.size());
    for (const auto& value : values) {
        normalized.push_back(detail::normalize_value(value));
    }
    return Array(std::make_shared<RegularArray<Storage>>(std::move(normalized), size));
}

}  // namespace ak
