#pragma once

#include "awkward/contents/content.hpp"
#include "awkward/contents/list_offset_content_array.hpp"
#include "awkward/contents/numpy_array.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace ak {

class ListArray final : public Content {
public:
    ListArray(std::vector<std::size_t> starts,
              std::vector<std::size_t> stops,
              std::shared_ptr<const Content> content)
        : starts_(std::move(starts)), stops_(std::move(stops)), content_(std::move(content)) {
        const auto error = validate();
        if (!error.empty()) throw std::invalid_argument(error);
    }

    LayoutKind kind() const noexcept override { return LayoutKind::list; }
    std::size_t length() const noexcept override { return starts_.size(); }

    std::size_t nbytes() const noexcept override {
        return (starts_.size() + stops_.size()) * sizeof(std::size_t) + content_->nbytes();
    }

    std::string typestr() const override {
        return detail::list_type_string(length(), detail::item_type_from_typestr(content_->typestr()));
    }

    std::string validity_error() const override { return validate(); }

    Value to_list() const override {
        Value::list_type result;
        result.reserve(length());
        for (std::size_t row = 0; row < length(); ++row) result.push_back(row_value(row));
        return result;
    }

    Value at(std::ptrdiff_t index) const override {
        return row_value(index::detail::normalize_integer(index, length(), "row"));
    }

    Value at(std::ptrdiff_t outer, std::ptrdiff_t inner) const override {
        const auto row = index::detail::normalize_integer(outer, length(), "row");
        const auto column = index::detail::normalize_integer(inner, stops_[row] - starts_[row], "column");
        return content_->at(static_cast<std::ptrdiff_t>(starts_[row] + column));
    }

    std::shared_ptr<const Content> slice_rows(const index::Slice& slice) const override {
        return select(index::detail::indices_for_slice(slice, length()));
    }

    std::shared_ptr<const Content> take_rows(const std::vector<std::ptrdiff_t>& indices) const override {
        return select(index::detail::normalize_integer_array(indices, length(), "row"));
    }

    std::shared_ptr<const Content> mask_rows(const std::vector<bool>& mask) const override {
        if (mask.size() != length()) throw std::invalid_argument("boolean mask length must match row count");
        std::vector<std::size_t> selected;
        for (std::size_t i = 0; i < mask.size(); ++i) if (mask[i]) selected.push_back(i);
        return select(selected);
    }

    std::shared_ptr<const Content> mask_as_array(const Content& mask) const override {
        const auto* values = mask.flat_bool_mask();
        if (!values) throw std::invalid_argument("list array requires a flat boolean mask");
        return mask_rows(*values);
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
        return select_inner_array(index::detail::normalize_integer_array(rows, length(), "row"), columns, false)
            ->flatten();
    }

    std::shared_ptr<const Content> take_inner_array(const std::vector<std::ptrdiff_t>& rows,
                                                    const std::vector<std::ptrdiff_t>& columns) const override {
        return select_inner_array(index::detail::normalize_integer_array(rows, length(), "row"), columns, false);
    }

    std::shared_ptr<const Content> take_one_inner_array(
        std::ptrdiff_t row, const std::vector<std::ptrdiff_t>& columns) const override {
        return select_inner_array({index::detail::normalize_integer(row, length(), "row")}, columns, true);
    }

    std::vector<std::string> fields() const override { return content_->fields(); }
    bool is_tuple() const noexcept override { return content_->is_tuple(); }
    std::string record_name() const override { return content_->record_name(); }

    std::shared_ptr<const Content> field(const std::string& name) const override {
        return std::make_shared<ListArray>(starts_, stops_, content_->field(name));
    }

    std::shared_ptr<const Content> project_fields(const std::vector<std::string>& names) const override {
        return std::make_shared<ListArray>(starts_, stops_, content_->project_fields(names));
    }

    std::shared_ptr<const Content> with_name(std::string name) const override {
        return std::make_shared<ListArray>(starts_, stops_, content_->with_name(std::move(name)));
    }

    std::vector<std::size_t> num() const override {
        std::vector<std::size_t> result;
        result.reserve(length());
        for (std::size_t i = 0; i < length(); ++i) result.push_back(stops_[i] - starts_[i]);
        return result;
    }

    std::shared_ptr<const Content> flatten() const override {
        return content_->take_rows(flat_indices())->to_packed();
    }

    std::shared_ptr<const Content> to_packed() const override {
        auto indices = flat_indices();
        std::vector<std::size_t> offsets;
        offsets.reserve(length() + 1);
        offsets.push_back(0);
        for (std::size_t row = 0; row < length(); ++row) offsets.push_back(offsets.back() + stops_[row] - starts_[row]);
        return std::make_shared<ListOffsetContentArray>(content_->take_rows(indices)->to_packed(), std::move(offsets));
    }

    std::shared_ptr<const Content> local_index() const override {
        std::vector<std::int64_t> values;
        std::vector<std::size_t> offsets;
        offsets.reserve(length() + 1);
        offsets.push_back(0);
        for (std::size_t row = 0; row < length(); ++row) {
            for (std::size_t i = starts_[row]; i < stops_[row]; ++i) values.push_back(static_cast<std::int64_t>(i - starts_[row]));
            offsets.push_back(values.size());
        }
        return std::make_shared<ListOffsetContentArray>(
            std::make_shared<NumpyArray<std::int64_t>>(std::move(values)), std::move(offsets));
    }

    const std::vector<std::size_t>& starts() const noexcept { return starts_; }
    const std::vector<std::size_t>& stops() const noexcept { return stops_; }
    const Content& content() const noexcept { return *content_; }

    Form to_buffers(detail::BufferBuilder& builder) const override {
        auto key = builder.next_key();
        builder.add(key + "-starts", detail::index_buffer_from_offsets(starts_));
        builder.add(key + "-stops", detail::index_buffer_from_offsets(stops_));
        auto content_form = content_->to_buffers(builder);
        return Form{
            .kind = FormKind::list,
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
            for (const auto column :
                 index::detail::indices_for_slice(columns, stops_[row] - starts_[row])) {
                selected.push_back(static_cast<std::ptrdiff_t>(starts_[row] + column));
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
            for (const auto column :
                 index::detail::normalize_integer_array(columns, stops_[row] - starts_[row], "column")) {
                selected.push_back(static_cast<std::ptrdiff_t>(starts_[row] + column));
            }
            offsets.push_back(selected.size());
        }
        auto content = std::make_shared<IndexedArray>(std::move(selected), content_);
        if (drop_outer) return content;
        return std::make_shared<ListOffsetContentArray>(std::move(content), std::move(offsets));
    }

    Value row_value(std::size_t row) const {
        Value::list_type result;
        result.reserve(stops_[row] - starts_[row]);
        for (std::size_t i = starts_[row]; i < stops_[row]; ++i) result.push_back(content_->at(static_cast<std::ptrdiff_t>(i)));
        return result;
    }

    std::vector<std::ptrdiff_t> flat_indices() const {
        std::vector<std::ptrdiff_t> result;
        for (std::size_t row = 0; row < length(); ++row) {
            for (std::size_t i = starts_[row]; i < stops_[row]; ++i) result.push_back(static_cast<std::ptrdiff_t>(i));
        }
        return result;
    }

    std::shared_ptr<const Content> select(const std::vector<std::size_t>& rows) const {
        std::vector<std::size_t> starts;
        std::vector<std::size_t> stops;
        starts.reserve(rows.size());
        stops.reserve(rows.size());
        for (const auto row : rows) {
            starts.push_back(starts_[row]);
            stops.push_back(stops_[row]);
        }
        return std::make_shared<ListArray>(std::move(starts), std::move(stops), content_);
    }

    std::string validate() const {
        if (!content_) return "ak::ListArray content must not be null";
        const auto error = content_->validity_error();
        if (!error.empty()) return error;
        if (starts_.size() != stops_.size()) return "ak::ListArray starts and stops must have matching lengths";
        for (std::size_t i = 0; i < starts_.size(); ++i) {
            if (starts_[i] > stops_[i]) return "ak::ListArray start must not exceed stop";
            if (stops_[i] > content_->length()) return "ak::ListArray stop must not exceed content length";
        }
        return {};
    }

    std::vector<std::size_t> starts_;
    std::vector<std::size_t> stops_;
    std::shared_ptr<const Content> content_;
};

}  // namespace ak
