#pragma once

#include "awkward/contents/content.hpp"
#include "awkward/contents/empty_array.hpp"
#include "awkward/contents/list_offset_content_array.hpp"
#include "awkward/contents/numpy_array.hpp"
#include "awkward/index.hpp"
#include "awkward/types.hpp"
#include "awkward/value.hpp"

#include <algorithm>
#include <concepts>
#include <initializer_list>
#include <map>
#include <memory>
#include <ostream>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace ak {

namespace detail {

std::shared_ptr<const Content> layout_from_list(const Value::list_type& values);

inline std::size_t layout_ndim(const Content& layout) {
    BufferBuilder builder;
    return type_from_form(layout.to_buffers(builder))->ndim() + 1;
}

inline std::vector<index::Item> normalize_slice_items(const Content& layout,
                                                      const std::vector<index::Item>& items) {
    std::vector<index::Item> normalized;
    std::size_t ellipsis_count = 0;
    std::size_t consuming_items = 0;
    for (const auto& item : items) {
        if (std::holds_alternative<index::Ellipsis>(item)) {
            ++ellipsis_count;
        } else if (!std::holds_alternative<index::NewAxis>(item) &&
                   !std::holds_alternative<index::Field>(item) &&
                   !std::holds_alternative<index::Fields>(item)) {
            ++consuming_items;
        }
    }
    if (ellipsis_count > 1) {
        throw std::invalid_argument("a slice may contain at most one ellipsis");
    }
    if (ellipsis_count == 0) return items;

    const auto ndim = layout_ndim(layout);
    if (consuming_items > ndim) {
        throw std::invalid_argument("slice has more dimension indexes than the array");
    }
    normalized.reserve(items.size() + ndim - consuming_items);
    for (const auto& item : items) {
        if (std::holds_alternative<index::Ellipsis>(item)) {
            for (std::size_t i = consuming_items; i < ndim; ++i) normalized.push_back(index::all());
        } else {
            normalized.push_back(item);
        }
    }
    return normalized;
}

inline std::shared_ptr<const Content> add_outer_axis(const Content& layout) {
    return std::make_shared<ListOffsetContentArray>(
        layout.to_packed(), std::vector<std::size_t>{0, layout.length()});
}

inline std::shared_ptr<const Content> add_singleton_axis(std::shared_ptr<const Content> layout) {
    std::vector<std::size_t> offsets;
    offsets.reserve(layout->length() + 1);
    for (std::size_t i = 0; i <= layout->length(); ++i) offsets.push_back(i);
    return std::make_shared<ListOffsetContentArray>(std::move(layout), std::move(offsets));
}

inline Value project_value_field(const Value& value, const std::vector<std::string>& names, bool single) {
    if (value.is_none()) return Value(nullptr);
    if (const auto* values = std::get_if<Value::list_type>(&value.storage())) {
        Value::list_type result;
        result.reserve(values->size());
        for (const auto& item : *values) result.push_back(project_value_field(item, names, single));
        return result;
    }
    const auto* record = std::get_if<Value::record_type>(&value.storage());
    if (!record) throw std::invalid_argument("field indexing requires record values");
    if (single) {
        const auto found = std::find(record->fields.begin(), record->fields.end(), names.front());
        if (found == record->fields.end()) throw std::out_of_range("record field does not exist: " + names.front());
        return record->values[static_cast<std::size_t>(std::distance(record->fields.begin(), found))];
    }
    Value::record_type result;
    result.is_tuple = record->is_tuple;
    result.fields = names;
    for (const auto& name : names) {
        const auto found = std::find(record->fields.begin(), record->fields.end(), name);
        if (found == record->fields.end()) throw std::out_of_range("record field does not exist: " + name);
        result.values.push_back(record->values[static_cast<std::size_t>(std::distance(record->fields.begin(), found))]);
    }
    return result;
}

inline Value slice_value_recursive(const Value& value,
                                   const std::vector<index::Item>& items,
                                   std::size_t position) {
    if (position == items.size()) return value;
    const auto& item = items[position];
    if (const auto* field = std::get_if<index::Field>(&item)) {
        return slice_value_recursive(project_value_field(value, {field->name}, true), items, position + 1);
    }
    if (const auto* fields = std::get_if<index::Fields>(&item)) {
        return slice_value_recursive(project_value_field(value, fields->names, false), items, position + 1);
    }
    if (std::holds_alternative<index::NewAxis>(item)) {
        return Value::list_type{slice_value_recursive(value, items, position + 1)};
    }
    if (value.is_none()) return Value(nullptr);
    const auto* values = std::get_if<Value::list_type>(&value.storage());
    if (!values) throw std::invalid_argument("slice index is deeper than an input value");

    if (const auto* integer = std::get_if<index::Integer>(&item)) {
        const auto selected = index::detail::normalize_integer(integer->value, values->size(), "slice");
        return slice_value_recursive((*values)[selected], items, position + 1);
    }

    std::vector<std::size_t> selected;
    if (const auto* slice = std::get_if<index::Slice>(&item)) {
        selected = index::detail::indices_for_slice(*slice, values->size());
    } else if (const auto* integers = std::get_if<index::IntegerArray>(&item)) {
        selected = index::detail::normalize_integer_array(integers->values, values->size(), "slice");
    } else if (const auto* booleans = std::get_if<index::BooleanArray>(&item)) {
        if (booleans->values.size() != values->size()) {
            throw std::invalid_argument("boolean mask length must match the selected dimension");
        }
        for (std::size_t i = 0; i < booleans->values.size(); ++i) if (booleans->values[i]) selected.push_back(i);
    } else {
        throw std::invalid_argument("array-backed masks are only supported as standalone indexes");
    }

    Value::list_type result;
    result.reserve(selected.size());
    for (const auto selected_index : selected) {
        result.push_back(slice_value_recursive((*values)[selected_index], items, position + 1));
    }
    return result;
}

inline std::vector<std::ptrdiff_t> row_indices_from_item(const Content& layout, const index::Item& item) {
    if (const auto* integer = std::get_if<index::Integer>(&item)) {
        return {integer->value};
    }
    if (const auto* integers = std::get_if<index::IntegerArray>(&item)) {
        return integers->values;
    }
    if (const auto* slice = std::get_if<index::Slice>(&item)) {
        const auto indices = index::detail::indices_for_slice(*slice, layout.length());
        std::vector<std::ptrdiff_t> result;
        result.reserve(indices.size());
        for (const auto index : indices) {
            result.push_back(static_cast<std::ptrdiff_t>(index));
        }
        return result;
    }
    throw std::invalid_argument("nested slicing requires row integer, integer-array, or range indexing");
}

inline std::shared_ptr<const Content> slice_layout(const Content& layout, const std::vector<index::Item>& items) {
    const auto normalized = normalize_slice_items(layout, items);
    if (normalized.empty()) {
        return layout.to_packed();
    }
    const auto array_mask = std::find_if(normalized.begin(), normalized.end(), [](const auto& item) {
        return std::holds_alternative<index::ArrayIndex>(item);
    });
    if (array_mask != normalized.end() && normalized.size() > 1) {
        const auto& mask_item = std::get<index::ArrayIndex>(*array_mask);
        if (!mask_item.layout) throw std::invalid_argument("array index layout must not be null");
        const std::vector<index::Item> prefix(normalized.begin(), array_mask);
        const std::vector<index::Item> suffix(std::next(array_mask), normalized.end());
        auto selected = prefix.empty() ? layout.to_packed() : slice_layout(layout, prefix);
        auto selected_mask = prefix.empty() ? mask_item.layout->to_packed() : slice_layout(*mask_item.layout, prefix);
        auto masked_layout = selected->mask_as_array(*selected_mask);
        return suffix.empty() ? masked_layout : slice_layout(*masked_layout, suffix);
    }
    const auto generic_nested = normalized.size() > 1 &&
                                (layout.kind() == LayoutKind::indexed ||
                                 layout.kind() == LayoutKind::indexed_option ||
                                 layout.kind() == LayoutKind::byte_masked ||
                                 layout.kind() == LayoutKind::bit_masked ||
                                 layout.kind() == LayoutKind::unmasked ||
                                 layout.kind() == LayoutKind::union_);
    if (normalized.size() > 2 || generic_nested) {
        auto result = slice_value_recursive(layout.to_list(), normalized, 0);
        if (!std::holds_alternative<Value::list_type>(result.storage())) {
            result = Value::list_type{std::move(result)};
        }
        return layout_from_list(result.as_list());
    }

    const auto& first = normalized.front();
    if (normalized.size() == 1) {
        if (std::holds_alternative<index::NewAxis>(first)) {
            return add_outer_axis(layout);
        }
        if (const auto* field = std::get_if<index::Field>(&first)) {
            return layout.field(field->name);
        }
        if (const auto* fields = std::get_if<index::Fields>(&first)) {
            return layout.project_fields(fields->names);
        }
        if (const auto* integer = std::get_if<index::Integer>(&first)) {
            return layout.take_rows({integer->value});
        }
        if (const auto* slice = std::get_if<index::Slice>(&first)) {
            return layout.slice_rows(*slice);
        }
        if (const auto* integers = std::get_if<index::IntegerArray>(&first)) {
            return layout.take_rows(integers->values);
        }
        if (const auto* booleans = std::get_if<index::BooleanArray>(&first)) {
            return layout.mask_rows(booleans->values);
        }
        if (const auto* array_index = std::get_if<index::ArrayIndex>(&first)) {
            if (!array_index->layout) {
                throw std::invalid_argument("array index layout must not be null");
            }
            return layout.mask_as_array(*array_index->layout);
        }
        throw std::invalid_argument("unsupported slice item");
    }

    const auto& second = normalized[1];
    if (std::holds_alternative<index::NewAxis>(first)) {
        if (const auto* integer = std::get_if<index::Integer>(&second)) {
            return layout.take_rows({integer->value});
        }
        if (const auto* slice = std::get_if<index::Slice>(&second)) {
            return add_outer_axis(*layout.slice_rows(*slice));
        }
        if (const auto* integers = std::get_if<index::IntegerArray>(&second)) {
            return add_outer_axis(*layout.take_rows(integers->values));
        }
        throw std::invalid_argument("newaxis inner indexing requires an integer, integer array, or range");
    }
    if (std::holds_alternative<index::NewAxis>(second)) {
        return add_singleton_axis(slice_layout(layout, {first}));
    }
    if (const auto* field = std::get_if<index::Field>(&second)) {
        return slice_layout(*slice_layout(layout, {first}), {*field});
    }
    if (const auto* fields = std::get_if<index::Fields>(&second)) {
        return slice_layout(*slice_layout(layout, {first}), {*fields});
    }
    if (const auto* column_slice = std::get_if<index::Slice>(&second)) {
        index::Slice row_slice;
        if (const auto* row_integer = std::get_if<index::Integer>(&first)) {
            return layout.slice_one_inner(row_integer->value, *column_slice);
        }
        if (const auto* first_slice = std::get_if<index::Slice>(&first)) {
            row_slice = *first_slice;
        } else {
            const auto rows = row_indices_from_item(layout, first);
            return layout.take_rows(rows)->slice_inner(index::Slice{}, *column_slice);
        }
        return layout.slice_inner(row_slice, *column_slice);
    }
    if (const auto* column = std::get_if<index::Integer>(&second)) {
        return layout.take_inner(row_indices_from_item(layout, first), column->value);
    }
    if (const auto* columns = std::get_if<index::IntegerArray>(&second)) {
        if (const auto* row = std::get_if<index::Integer>(&first)) {
            return layout.take_one_inner_array(row->value, columns->values);
        }
        return layout.take_inner_array(row_indices_from_item(layout, first), columns->values);
    }
    throw std::invalid_argument("inner indexing supports integer, integer-array, and range columns");
}

}  // namespace detail

class Record {
public:
    explicit Record(Value value) : value_(std::move(value)) {
        if (!std::holds_alternative<Value::record_type>(value_.storage())) {
            throw std::invalid_argument("ak::Record requires a record value");
        }
    }

    const Value& to_list() const noexcept {
        return value_;
    }

    std::vector<std::string> fields() const {
        return value_.as_record().fields;
    }

    bool is_tuple() const noexcept {
        return value_.as_record().is_tuple;
    }

    Value field(const std::string& name) const {
        const auto& record = value_.as_record();
        for (std::size_t i = 0; i < record.fields.size(); ++i) {
            if (record.fields[i] == name) {
                return record.values[i];
            }
        }
        throw std::out_of_range("record field does not exist: " + name);
    }

private:
    Value value_;
};

class Scalar {
public:
    explicit Scalar(Value value) : value_(std::move(value)), content_type_(detail::scalar_type_from_value(value_)) {}

    const Value& value() const noexcept {
        return value_;
    }

    ScalarType type() const {
        return ScalarType(content_type_);
    }

    std::string typestr() const {
        return content_type_->typestr();
    }

    std::size_t ndim() const noexcept {
        return content_type_->ndim();
    }

    void show(std::ostream& stream) const {
        stream << value_ << '\n';
    }

    operator const Value&() const noexcept {
        return value_;
    }

    friend bool operator==(const Scalar& left, const Value& right) {
        return left.value_ == right;
    }

    friend bool operator==(const Value& left, const Scalar& right) {
        return left == right.value_;
    }

private:
    Value value_;
    TypePtr content_type_;
};

template <typename T>
class ArrayView {
public:
    explicit ArrayView(std::shared_ptr<const Content> owner) requires (!std::same_as<T, bool>)
        : owner_(std::move(owner)), typed_(std::dynamic_pointer_cast<const NumpyArray<T>>(owner_)) {
        if (!typed_) throw std::invalid_argument("ak::Array layout does not match the requested typed view");
    }

    std::size_t size() const noexcept { return typed_->length(); }
    bool empty() const noexcept { return size() == 0; }
    const T* begin() const noexcept { return typed_->values().data(); }
    const T* end() const noexcept { return typed_->values().data() + typed_->values().size(); }
    const T& operator[](std::size_t index) const noexcept { return typed_->values()[index]; }

    const T& at(std::size_t index) const {
        if (index >= size()) throw std::out_of_range("ak::ArrayView index is out of range");
        return typed_->values()[index];
    }

    std::span<const T> values() const noexcept { return typed_->values(); }

private:
    std::shared_ptr<const Content> owner_;
    std::shared_ptr<const NumpyArray<T>> typed_;
};

class Array {
public:
    using Metadata = std::map<std::string, std::string>;
    using NamedAxes = std::map<std::string, int>;

    Array() : layout_(std::make_shared<EmptyArray>()) {}

    explicit Array(std::shared_ptr<const Content> layout,
                   Metadata behavior = {},
                   Metadata attrs = {},
                   NamedAxes named_axes = {})
        : layout_(std::move(layout)),
          behavior_(std::move(behavior)),
          attrs_(std::move(attrs)),
          named_axes_(std::move(named_axes)) {
        if (!layout_) {
            throw std::invalid_argument("ak::Array layout must not be null");
        }
        const auto error = layout_->validity_error();
        if (!error.empty()) {
            throw std::invalid_argument(error);
        }
        const auto dimensions = static_cast<int>(ndim());
        for (const auto& [name, axis] : named_axes_) {
            if (name.empty()) {
                throw std::invalid_argument("ak::Array named-axis names must not be empty");
            }
            if (axis < -dimensions || axis >= dimensions) {
                throw std::invalid_argument("ak::Array named axis is outside the array dimensions");
            }
        }
    }

    const Content& layout() const noexcept {
        return *layout_;
    }

    std::shared_ptr<const Content> layout_ptr() const noexcept {
        return layout_;
    }

    const Metadata& behavior() const noexcept {
        return behavior_;
    }

    const Metadata& attrs() const noexcept {
        return attrs_;
    }

    const NamedAxes& named_axes() const noexcept {
        return named_axes_;
    }

    Array with_layout(std::shared_ptr<const Content> layout) const {
        return Array(std::move(layout), behavior_, attrs_, named_axes_);
    }

    Array with_behavior(Metadata behavior) const {
        return Array(layout_, std::move(behavior), attrs_, named_axes_);
    }

    Array with_attrs(Metadata attrs) const {
        return Array(layout_, behavior_, std::move(attrs), named_axes_);
    }

    Array with_named_axes(NamedAxes named_axes) const {
        return Array(layout_, behavior_, attrs_, std::move(named_axes));
    }

    std::size_t length() const noexcept {
        return layout_->length();
    }

    std::size_t nbytes() const noexcept {
        return layout_->nbytes();
    }

    ArrayType type() const {
        detail::BufferBuilder builder;
        return ArrayType(detail::type_from_form(layout_->to_buffers(builder)), length());
    }

    std::string typestr() const {
        return layout_->typestr();
    }

    std::size_t ndim() const {
        return type().ndim();
    }

    std::string validity_error() const {
        return layout_->validity_error();
    }

    bool is_valid() const {
        return validity_error().empty();
    }

    Value to_list() const {
        return layout_->to_list();
    }

    Value at(std::ptrdiff_t index) const {
        return layout_->at(index);
    }

    Value at(std::ptrdiff_t outer, std::ptrdiff_t inner) const {
        return layout_->at(outer, inner);
    }

    Record record_at(std::ptrdiff_t index) const {
        return Record(at(index));
    }

    Scalar scalar_at(std::ptrdiff_t index) const {
        return Scalar(at(index));
    }

    template <typename T>
    ArrayView<T> view() const requires (!std::same_as<T, bool>) {
        return ArrayView<T>(layout_);
    }

    std::vector<std::string> fields() const {
        return layout_->fields();
    }

    bool is_tuple() const noexcept {
        return layout_->is_tuple();
    }

    Array field(const std::string& name) const {
        return with_layout(layout_->field(name));
    }

    Array project_fields(const std::vector<std::string>& names) const {
        return with_layout(layout_->project_fields(names));
    }

    Array slice(std::initializer_list<index::Item> items) const {
        return with_layout(detail::slice_layout(*layout_, std::vector<index::Item>(items.begin(), items.end())));
    }

    Array slice(const std::vector<index::Item>& items) const {
        return with_layout(detail::slice_layout(*layout_, items));
    }

    void show(std::ostream& stream) const {
        stream << to_list() << '\n';
    }

private:
    std::shared_ptr<const Content> layout_;
    Metadata behavior_;
    Metadata attrs_;
    NamedAxes named_axes_;
};

using ReducerResult = std::variant<Scalar, Array>;

}  // namespace ak
