#pragma once

#include "awkward/contents/content.hpp"
#include "awkward/contents/detail.hpp"
#include "awkward/contents/numpy_array.hpp"

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace ak {

template <typename T>
class ListOffsetArray final : public Content {
public:
    using value_type = T;

    ListOffsetArray(std::shared_ptr<const NumpyArray<T>> content, std::vector<std::size_t> offsets)
        : content_(std::move(content)), offsets_(std::move(offsets)) {
        const auto error = validate();
        if (!error.empty()) {
            throw std::invalid_argument(error);
        }
    }

    ListOffsetArray(std::vector<T> values, std::vector<std::size_t> offsets)
        : ListOffsetArray(std::make_shared<NumpyArray<T>>(std::move(values)), std::move(offsets)) {}

    LayoutKind kind() const noexcept override {
        return LayoutKind::list_offset;
    }

    std::size_t length() const noexcept override {
        return offsets_.size() - 1;
    }

    std::size_t nbytes() const noexcept override {
        return content_->nbytes() + offsets_.size() * sizeof(std::size_t);
    }

    std::string typestr() const override {
        return detail::list_type_string(length(), detail::primitive_type_name<T>());
    }

    std::string validity_error() const override {
        return validate();
    }

    Value to_list() const override {
        Value::list_type rows;
        rows.reserve(length());
        for (std::size_t row = 0; row < length(); ++row) {
            const auto start = offsets_[row];
            const auto stop = offsets_[row + 1];
            Value::list_type values;
            values.reserve(stop - start);
            for (auto i = start; i < stop; ++i) {
                values.emplace_back(detail::scalar_to_value(content_->values_vector()[i]));
            }
            rows.emplace_back(std::move(values));
        }
        return rows;
    }

    bool ragged_bool_mask(std::vector<bool>& values, std::vector<std::size_t>& offsets) const override {
        if constexpr (std::same_as<T, bool>) {
            values = content_->values_vector();
            offsets = offsets_;
            return true;
        } else {
            (void)values;
            (void)offsets;
            return false;
        }
    }

    Value at(std::ptrdiff_t index) const override {
        const auto row = index::detail::normalize_integer(index, length(), "row");
        return row_value(row);
    }

    Value at(std::ptrdiff_t outer, std::ptrdiff_t inner) const override {
        const auto row = index::detail::normalize_integer(outer, length(), "row");
        const auto start = offsets_[row];
        const auto stop = offsets_[row + 1];
        const auto column = index::detail::normalize_integer(inner, stop - start, "column");
        return detail::scalar_to_value(content_->values_vector()[start + column]);
    }

    std::shared_ptr<const Content> slice_rows(const index::Slice& slice) const override {
        return select_rows(index::detail::indices_for_slice(slice, length()));
    }

    std::shared_ptr<const Content> take_rows(const std::vector<std::ptrdiff_t>& indices) const override {
        return select_rows(index::detail::normalize_integer_array(indices, length(), "row"));
    }

    std::shared_ptr<const Content> mask_rows(const std::vector<bool>& mask) const override {
        return select_rows(rows_from_mask(mask));
    }

    std::shared_ptr<const Content> mask_as_array(const Content& mask) const override {
        if (const auto* flat_mask = mask.flat_bool_mask()) {
            return mask_rows(*flat_mask);
        }
        std::vector<bool> mask_values;
        std::vector<std::size_t> mask_offsets;
        if (mask.ragged_bool_mask(mask_values, mask_offsets)) {
            return apply_ragged_mask(mask_values, mask_offsets);
        }
        throw std::invalid_argument("array indexing requires a boolean mask");
    }

    std::shared_ptr<const Content> slice_inner(const index::Slice& rows, const index::Slice& columns) const override {
        return select_inner_slice(index::detail::indices_for_slice(rows, length()), columns);
    }

    std::shared_ptr<const Content> slice_one_inner(std::ptrdiff_t row, const index::Slice& columns) const override {
        const auto row_index = index::detail::normalize_integer(row, length(), "row");
        const auto start = offsets_[row_index];
        const auto stop = offsets_[row_index + 1];
        std::vector<T> values;
        for (const auto column : index::detail::indices_for_slice(columns, stop - start)) {
            values.push_back(content_->values_vector()[start + column]);
        }
        return std::make_shared<NumpyArray<T>>(std::move(values));
    }

    std::shared_ptr<const Content> take_inner(const std::vector<std::ptrdiff_t>& rows,
                                              std::ptrdiff_t column) const override {
        return select_inner_integer(index::detail::normalize_integer_array(rows, length(), "row"), column);
    }

    std::shared_ptr<const Content> take_inner_array(const std::vector<std::ptrdiff_t>& rows,
                                                    const std::vector<std::ptrdiff_t>& columns) const override {
        return select_inner_array(index::detail::normalize_integer_array(rows, length(), "row"), columns, false);
    }

    std::shared_ptr<const Content> take_one_inner_array(
        std::ptrdiff_t row, const std::vector<std::ptrdiff_t>& columns) const override {
        return select_inner_array({index::detail::normalize_integer(row, length(), "row")}, columns, true);
    }

    std::shared_ptr<const Content> slice_items(const std::vector<index::Item>& items) const {
        std::vector<index::Item> normalized_items;
        normalized_items.reserve(items.size());
        for (const auto& item : items) {
            if (std::holds_alternative<index::Ellipsis>(item)) {
                continue;
            }
            normalized_items.push_back(item);
        }
        if (normalized_items.empty()) {
            return to_packed();
        }
        if (normalized_items.size() == 1) {
            if (const auto* array_index = std::get_if<index::ArrayIndex>(&normalized_items.front())) {
                std::vector<bool> mask_values;
                std::vector<std::size_t> mask_offsets;
                if (array_index->layout->ragged_bool_mask(mask_values, mask_offsets)) {
                    return apply_ragged_mask(mask_values, mask_offsets);
                }
            }
        }
        if (normalized_items.size() > 2) {
            throw std::invalid_argument("low-level list-offset slicing supports at most row and column items");
        }

        const auto rows = rows_from_item(normalized_items.front());
        if (rows.dropped_dimension && normalized_items.size() == 1) {
            return make_row_content(rows.indices.front());
        }
        if (normalized_items.size() == 1) {
            return select_rows(rows.indices);
        }

        const auto& inner = normalized_items[1];
        if (const auto* integer = std::get_if<index::Integer>(&inner)) {
            return select_inner_integer(rows.indices, integer->value);
        }
        if (const auto* slice = std::get_if<index::Slice>(&inner)) {
            return select_inner_slice(rows.indices, *slice);
        }
        if (const auto* columns = std::get_if<index::IntegerArray>(&inner)) {
            return select_inner_array(rows.indices, columns->values, rows.dropped_dimension);
        }
        throw std::invalid_argument("unsupported inner slice item");
    }

    std::vector<std::size_t> num() const override {
        std::vector<std::size_t> result;
        result.reserve(length());
        for (std::size_t row = 0; row < length(); ++row) {
            result.push_back(offsets_[row + 1] - offsets_[row]);
        }
        return result;
    }

    std::shared_ptr<const Content> flatten() const override {
        return content_;
    }

    std::shared_ptr<const Content> to_packed() const override {
        return std::make_shared<ListOffsetArray<T>>(content_, offsets_);
    }

    std::shared_ptr<const Content> local_index() const override {
        std::vector<std::int64_t> values;
        std::vector<std::size_t> offsets;
        offsets.reserve(length() + 1);
        offsets.push_back(0);
        for (std::size_t row = 0; row < length(); ++row) {
            const auto row_length = offsets_[row + 1] - offsets_[row];
            for (std::size_t i = 0; i < row_length; ++i) {
                values.push_back(static_cast<std::int64_t>(i));
            }
            offsets.push_back(values.size());
        }
        return std::make_shared<ListOffsetArray<std::int64_t>>(std::move(values), std::move(offsets));
    }

    const NumpyArray<T>& content() const noexcept {
        return *content_;
    }

    std::span<const std::size_t> offsets() const noexcept {
        return std::span<const std::size_t>(offsets_.data(), offsets_.size());
    }

    Form to_buffers(detail::BufferBuilder& builder) const override {
        auto key = builder.next_key();
        builder.add(key + "-offsets", detail::index_buffer_from_offsets(offsets_));
        auto content_form = content_->to_buffers(builder);
        return Form{
            .kind = FormKind::list_offset,
            .key = std::move(key),
            .contents = {std::move(content_form)},
            .length = length(),
        };
    }

private:
    struct RowSelection {
        std::vector<std::size_t> indices;
        bool dropped_dimension{false};
    };

    Value row_value(std::size_t row) const {
        const auto start = offsets_[row];
        const auto stop = offsets_[row + 1];
        Value::list_type values;
        values.reserve(stop - start);
        for (auto i = start; i < stop; ++i) {
            values.emplace_back(detail::scalar_to_value(content_->values_vector()[i]));
        }
        return values;
    }

    RowSelection rows_from_item(const index::Item& item) const {
        if (const auto* integer = std::get_if<index::Integer>(&item)) {
            return RowSelection{{index::detail::normalize_integer(integer->value, length(), "row")}, true};
        }
        if (const auto* slice = std::get_if<index::Slice>(&item)) {
            return RowSelection{index::detail::indices_for_slice(*slice, length()), false};
        }
        if (const auto* integers = std::get_if<index::IntegerArray>(&item)) {
            return RowSelection{index::detail::normalize_integer_array(integers->values, length(), "row"), false};
        }
        if (const auto* booleans = std::get_if<index::BooleanArray>(&item)) {
            return RowSelection{rows_from_mask(booleans->values), false};
        }
        if (const auto* array_index = std::get_if<index::ArrayIndex>(&item)) {
            if (const auto* flat_mask = array_index->layout->flat_bool_mask()) {
                return RowSelection{rows_from_mask(*flat_mask), false};
            }
            throw std::invalid_argument("array indexing requires a boolean mask");
        }
        throw std::invalid_argument("unsupported row slice item");
    }

    std::vector<std::size_t> rows_from_mask(const std::vector<bool>& mask) const {
        if (mask.size() != length()) {
            throw std::invalid_argument("boolean mask length must match row count");
        }
        std::vector<std::size_t> rows;
        for (std::size_t i = 0; i < mask.size(); ++i) {
            if (mask[i]) {
                rows.push_back(i);
            }
        }
        return rows;
    }

    std::shared_ptr<const Content> make_row_content(std::size_t row) const {
        const auto start = offsets_[row];
        const auto stop = offsets_[row + 1];
        std::vector<T> values;
        values.reserve(stop - start);
        values.insert(values.end(), content_->values_vector().begin() + static_cast<std::ptrdiff_t>(start),
                      content_->values_vector().begin() + static_cast<std::ptrdiff_t>(stop));
        return std::make_shared<NumpyArray<T>>(std::move(values));
    }

    std::shared_ptr<const Content> select_rows(const std::vector<std::size_t>& rows) const {
        std::vector<T> values;
        std::vector<std::size_t> offsets;
        offsets.reserve(rows.size() + 1);
        offsets.push_back(0);
        for (const auto row : rows) {
            const auto start = offsets_[row];
            const auto stop = offsets_[row + 1];
            values.insert(values.end(), content_->values_vector().begin() + static_cast<std::ptrdiff_t>(start),
                          content_->values_vector().begin() + static_cast<std::ptrdiff_t>(stop));
            offsets.push_back(values.size());
        }
        return std::make_shared<ListOffsetArray<T>>(std::move(values), std::move(offsets));
    }

    std::shared_ptr<const Content> select_inner_integer(const std::vector<std::size_t>& rows,
                                                        std::ptrdiff_t column_index) const {
        std::vector<T> values;
        values.reserve(rows.size());
        for (const auto row : rows) {
            const auto start = offsets_[row];
            const auto stop = offsets_[row + 1];
            const auto column = index::detail::normalize_integer(column_index, stop - start, "column");
            values.push_back(content_->values_vector()[start + column]);
        }
        return std::make_shared<NumpyArray<T>>(std::move(values));
    }

    std::shared_ptr<const Content> select_inner_slice(const std::vector<std::size_t>& rows,
                                                      const index::Slice& slice) const {
        std::vector<T> values;
        std::vector<std::size_t> offsets;
        offsets.reserve(rows.size() + 1);
        offsets.push_back(0);
        for (const auto row : rows) {
            const auto start = offsets_[row];
            const auto stop = offsets_[row + 1];
            for (const auto index : index::detail::indices_for_slice(slice, stop - start)) {
                values.push_back(content_->values_vector()[start + index]);
            }
            offsets.push_back(values.size());
        }
        return std::make_shared<ListOffsetArray<T>>(std::move(values), std::move(offsets));
    }

    std::shared_ptr<const Content> select_inner_array(const std::vector<std::size_t>& rows,
                                                      const std::vector<std::ptrdiff_t>& columns,
                                                      bool drop_outer) const {
        std::vector<T> values;
        std::vector<std::size_t> offsets;
        offsets.reserve(rows.size() + 1);
        offsets.push_back(0);
        for (const auto row : rows) {
            const auto start = offsets_[row];
            const auto stop = offsets_[row + 1];
            for (const auto column : index::detail::normalize_integer_array(columns, stop - start, "column")) {
                values.push_back(content_->values_vector()[start + column]);
            }
            offsets.push_back(values.size());
        }
        if (drop_outer) return std::make_shared<NumpyArray<T>>(std::move(values));
        return std::make_shared<ListOffsetArray<T>>(std::move(values), std::move(offsets));
    }

    std::shared_ptr<const Content> apply_ragged_mask(const std::vector<bool>& mask_values,
                                                     const std::vector<std::size_t>& mask_offsets) const {
        if (mask_offsets.empty() || mask_offsets.size() - 1 != length()) {
            throw std::invalid_argument("ragged boolean mask row count must match array row count");
        }

        std::vector<T> values;
        std::vector<std::size_t> offsets;
        offsets.reserve(length() + 1);
        offsets.push_back(0);

        for (std::size_t row = 0; row < length(); ++row) {
            const auto start = offsets_[row];
            const auto stop = offsets_[row + 1];
            if (mask_offsets[row + 1] - mask_offsets[row] != stop - start) {
                throw std::invalid_argument("ragged boolean mask row lengths must match array row lengths");
            }
            for (std::size_t i = 0; i < stop - start; ++i) {
                if (mask_values[mask_offsets[row] + i]) {
                    values.push_back(content_->values_vector()[start + i]);
                }
            }
            offsets.push_back(values.size());
        }

        return std::make_shared<ListOffsetArray<T>>(std::move(values), std::move(offsets));
    }

    std::string validate() const {
        if (!content_) {
            return "ak::ListOffsetArray content must not be null";
        }
        if (offsets_.empty()) {
            return "ak::ListOffsetArray offsets must contain at least the initial zero offset";
        }
        if (offsets_.front() != 0) {
            return "ak::ListOffsetArray offsets must start at zero";
        }
        if (offsets_.back() != content_->length()) {
            return "ak::ListOffsetArray final offset must equal content length";
        }
        if (!std::is_sorted(offsets_.begin(), offsets_.end())) {
            return "ak::ListOffsetArray offsets must be monotonic";
        }
        return {};
    }

    std::shared_ptr<const NumpyArray<T>> content_;
    std::vector<std::size_t> offsets_;
};

template <typename T>
std::shared_ptr<const Content> NumpyArray<T>::unflatten(const std::vector<std::size_t>& counts) const {
    std::vector<std::size_t> offsets;
    offsets.reserve(counts.size() + 1);
    offsets.push_back(0);

    std::size_t total = 0;
    for (const auto count : counts) {
        total += count;
        offsets.push_back(total);
    }
    if (total != values_.size()) {
        throw std::invalid_argument("ak::unflatten counts must sum to array length");
    }

    return std::make_shared<ListOffsetArray<T>>(std::make_shared<NumpyArray<T>>(values_), std::move(offsets));
}

}  // namespace ak
