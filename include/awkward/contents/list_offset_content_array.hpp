#pragma once

#include "awkward/contents/content.hpp"
#include "awkward/contents/detail.hpp"
#include "awkward/contents/indexed_array.hpp"
#include "awkward/contents/numpy_array.hpp"
#include "awkward/contents/option_array.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace ak {

class ListOffsetContentArray final : public Content {
public:
    ListOffsetContentArray(std::shared_ptr<const Content> content, std::vector<std::size_t> offsets)
        : content_(std::move(content)), offsets_(std::move(offsets)) {
        const auto error = validate();
        if (!error.empty()) {
            throw std::invalid_argument(error);
        }
    }

    LayoutKind kind() const noexcept override {
        return LayoutKind::list_offset_content;
    }

    std::size_t length() const noexcept override {
        return offsets_.size() - 1;
    }

    std::size_t nbytes() const noexcept override {
        return content_->nbytes() + offsets_.size() * sizeof(std::size_t);
    }

    std::string typestr() const override {
        return detail::list_type_string(length(), detail::item_type_from_typestr(content_->typestr()));
    }

    std::string validity_error() const override {
        return validate();
    }

    Value to_list() const override {
        Value::list_type rows;
        rows.reserve(length());
        for (std::size_t row = 0; row < length(); ++row) {
            Value::list_type values;
            values.reserve(offsets_[row + 1] - offsets_[row]);
            for (auto i = offsets_[row]; i < offsets_[row + 1]; ++i) {
                values.emplace_back(content_->at(static_cast<std::ptrdiff_t>(i)));
            }
            rows.emplace_back(std::move(values));
        }
        return rows;
    }

    Value at(std::ptrdiff_t index) const override {
        const auto row = index::detail::normalize_integer(index, length(), "row");
        Value::list_type values;
        values.reserve(offsets_[row + 1] - offsets_[row]);
        for (auto i = offsets_[row]; i < offsets_[row + 1]; ++i) {
            values.emplace_back(content_->at(static_cast<std::ptrdiff_t>(i)));
        }
        return values;
    }

    Value at(std::ptrdiff_t outer, std::ptrdiff_t inner) const override {
        const auto row = index::detail::normalize_integer(outer, length(), "row");
        const auto start = offsets_[row];
        const auto stop = offsets_[row + 1];
        const auto column = index::detail::normalize_integer(inner, stop - start, "column");
        return content_->at(static_cast<std::ptrdiff_t>(start + column));
    }

    std::shared_ptr<const Content> slice_rows(const index::Slice& slice) const override {
        return select_rows(index::detail::indices_for_slice(slice, length()));
    }

    std::shared_ptr<const Content> take_rows(const std::vector<std::ptrdiff_t>& indices) const override {
        return select_rows(index::detail::normalize_integer_array(indices, length(), "row"));
    }

    std::shared_ptr<const Content> mask_rows(const std::vector<bool>& mask) const override {
        if (mask.size() != length()) {
            throw std::invalid_argument("boolean mask length must match row count");
        }
        std::vector<std::size_t> rows;
        for (std::size_t i = 0; i < mask.size(); ++i) {
            if (mask[i]) {
                rows.push_back(i);
            }
        }
        return select_rows(rows);
    }

    std::shared_ptr<const Content> slice_inner(const index::Slice& rows,
                                               const index::Slice& columns) const override {
        return select_inner_slice(index::detail::indices_for_slice(rows, length()), columns, false);
    }

    std::shared_ptr<const Content> slice_one_inner(std::ptrdiff_t row,
                                                   const index::Slice& columns) const override {
        return select_inner_slice({index::detail::normalize_integer(row, length(), "row")}, columns, true);
    }

    std::shared_ptr<const Content> take_inner(const std::vector<std::ptrdiff_t>& rows,
                                              std::ptrdiff_t column) const override {
        const std::vector<std::ptrdiff_t> columns{column};
        auto selected = select_inner_array(index::detail::normalize_integer_array(rows, length(), "row"), columns, false);
        return selected->flatten();
    }

    std::shared_ptr<const Content> take_inner_array(const std::vector<std::ptrdiff_t>& rows,
                                                    const std::vector<std::ptrdiff_t>& columns) const override {
        return select_inner_array(index::detail::normalize_integer_array(rows, length(), "row"), columns, false);
    }

    std::shared_ptr<const Content> take_one_inner_array(
        std::ptrdiff_t row, const std::vector<std::ptrdiff_t>& columns) const override {
        return select_inner_array({index::detail::normalize_integer(row, length(), "row")}, columns, true);
    }

    std::vector<std::string> fields() const override {
        return content_->fields();
    }

    bool is_tuple() const noexcept override {
        return content_->is_tuple();
    }

    std::string record_name() const override {
        return content_->record_name();
    }

    std::shared_ptr<const Content> field(const std::string& name) const override {
        return std::make_shared<ListOffsetContentArray>(content_->field(name), offsets_);
    }

    std::shared_ptr<const Content> project_fields(const std::vector<std::string>& names) const override {
        return std::make_shared<ListOffsetContentArray>(content_->project_fields(names), offsets_);
    }

    std::shared_ptr<const Content> with_name(std::string name) const override {
        return std::make_shared<ListOffsetContentArray>(content_->with_name(std::move(name)), offsets_);
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
        return std::make_shared<ListOffsetContentArray>(content_->to_packed(), offsets_);
    }

    std::shared_ptr<const Content> local_index() const override {
        std::vector<std::int64_t> values;
        std::vector<std::size_t> offsets;
        offsets.reserve(length() + 1);
        offsets.push_back(0);
        for (std::size_t row = 0; row < length(); ++row) {
            for (std::size_t i = 0; i < offsets_[row + 1] - offsets_[row]; ++i) {
                values.push_back(static_cast<std::int64_t>(i));
            }
            offsets.push_back(values.size());
        }
        return std::make_shared<ListOffsetContentArray>(
            std::make_shared<NumpyArray<std::int64_t>>(std::move(values)), std::move(offsets));
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
    std::shared_ptr<const Content> select_inner_slice(const std::vector<std::size_t>& rows,
                                                      const index::Slice& columns,
                                                      bool drop_outer) const {
        std::vector<std::ptrdiff_t> selected;
        std::vector<std::size_t> offsets;
        offsets.reserve(rows.size() + 1);
        offsets.push_back(0);
        for (const auto row : rows) {
            const auto start = offsets_[row];
            const auto stop = offsets_[row + 1];
            for (const auto column : index::detail::indices_for_slice(columns, stop - start)) {
                selected.push_back(static_cast<std::ptrdiff_t>(start + column));
            }
            offsets.push_back(selected.size());
        }
        auto content = std::make_shared<IndexedArray>(std::move(selected), content_);
        if (drop_outer) return content;
        return std::make_shared<ListOffsetContentArray>(std::move(content), std::move(offsets));
    }

    std::shared_ptr<const Content> select_inner_array(const std::vector<std::size_t>& rows,
                                                      const std::vector<std::ptrdiff_t>& columns,
                                                      bool drop_outer) const {
        std::vector<std::ptrdiff_t> selected;
        std::vector<std::size_t> offsets;
        offsets.reserve(rows.size() + 1);
        offsets.push_back(0);
        for (const auto row : rows) {
            const auto start = offsets_[row];
            const auto stop = offsets_[row + 1];
            for (const auto column : index::detail::normalize_integer_array(columns, stop - start, "column")) {
                selected.push_back(static_cast<std::ptrdiff_t>(start + column));
            }
            offsets.push_back(selected.size());
        }
        auto content = std::make_shared<IndexedArray>(std::move(selected), content_);
        if (drop_outer) return content;
        return std::make_shared<ListOffsetContentArray>(std::move(content), std::move(offsets));
    }

    std::shared_ptr<const Content> select_rows(const std::vector<std::size_t>& rows) const {
        std::vector<std::ptrdiff_t> index;
        std::vector<std::size_t> offsets;
        offsets.reserve(rows.size() + 1);
        offsets.push_back(0);
        for (const auto row : rows) {
            for (auto i = offsets_[row]; i < offsets_[row + 1]; ++i) {
                index.push_back(static_cast<std::ptrdiff_t>(i));
            }
            offsets.push_back(index.size());
        }
        return std::make_shared<ListOffsetContentArray>(
            std::make_shared<IndexedArray>(std::move(index), content_), std::move(offsets));
    }

    std::string validate() const {
        if (!content_) {
            return "ak::ListOffsetContentArray content must not be null";
        }
        const auto content_error = content_->validity_error();
        if (!content_error.empty()) {
            return content_error;
        }
        if (offsets_.empty()) {
            return "ak::ListOffsetContentArray offsets must contain at least the initial zero offset";
        }
        if (offsets_.front() != 0) {
            return "ak::ListOffsetContentArray offsets must start at zero";
        }
        if (!std::is_sorted(offsets_.begin(), offsets_.end())) {
            return "ak::ListOffsetContentArray offsets must be monotonic";
        }
        if (offsets_.back() != content_->length()) {
            return "ak::ListOffsetContentArray final offset must equal content length";
        }
        return {};
    }

    std::shared_ptr<const Content> content_;
    std::vector<std::size_t> offsets_;
};

}  // namespace ak
