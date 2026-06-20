#pragma once

#include "awkward/contents/content.hpp"
#include "awkward/contents/detail.hpp"
#include "awkward/contents/numpy_array.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace ak {

class IndexedArray final : public Content {
public:
    IndexedArray(std::vector<std::ptrdiff_t> index, std::shared_ptr<const Content> content)
        : index_(std::move(index)), content_(std::move(content)) {
        const auto error = validate();
        if (!error.empty()) throw std::invalid_argument(error);
    }

    LayoutKind kind() const noexcept override { return LayoutKind::indexed; }
    std::size_t length() const noexcept override { return index_.size(); }
    std::size_t nbytes() const noexcept override { return index_.size() * sizeof(std::ptrdiff_t) + content_->nbytes(); }

    std::string typestr() const override {
        return std::to_string(length()) + " * " + detail::item_type_from_typestr(content_->typestr());
    }

    std::string validity_error() const override { return validate(); }

    Value to_list() const override {
        Value::list_type result;
        result.reserve(length());
        for (const auto item : index_) result.push_back(content_->at(item));
        return result;
    }

    Value at(std::ptrdiff_t index) const override {
        return content_->at(index_[index::detail::normalize_integer(index, length(), "array")]);
    }

    Value at(std::ptrdiff_t outer, std::ptrdiff_t inner) const override {
        return content_->at(index_[index::detail::normalize_integer(outer, length(), "row")], inner);
    }

    std::shared_ptr<const Content> slice_rows(const index::Slice& slice) const override {
        return select(index::detail::indices_for_slice(slice, length()));
    }

    std::shared_ptr<const Content> take_rows(const std::vector<std::ptrdiff_t>& indices) const override {
        return select(index::detail::normalize_integer_array(indices, length(), "array"));
    }

    std::shared_ptr<const Content> mask_rows(const std::vector<bool>& mask) const override {
        if (mask.size() != length()) throw std::invalid_argument("boolean mask length must match array length");
        std::vector<std::size_t> selected;
        for (std::size_t i = 0; i < mask.size(); ++i) if (mask[i]) selected.push_back(i);
        return select(selected);
    }

    std::shared_ptr<const Content> mask_as_array(const Content& mask) const override {
        const auto* values = mask.flat_bool_mask();
        if (!values) throw std::invalid_argument("indexed array requires a flat boolean mask");
        return mask_rows(*values);
    }

    std::vector<std::string> fields() const override { return content_->fields(); }
    bool is_tuple() const noexcept override { return content_->is_tuple(); }
    std::string record_name() const override { return content_->record_name(); }

    std::shared_ptr<const Content> field(const std::string& name) const override {
        return std::make_shared<IndexedArray>(index_, content_->field(name));
    }

    std::shared_ptr<const Content> project_fields(const std::vector<std::string>& names) const override {
        return std::make_shared<IndexedArray>(index_, content_->project_fields(names));
    }

    std::shared_ptr<const Content> with_name(std::string name) const override {
        return std::make_shared<IndexedArray>(index_, content_->with_name(std::move(name)));
    }

    std::vector<std::size_t> num() const override { return to_packed()->num(); }

    std::shared_ptr<const Content> slice_inner(const index::Slice& rows,
                                               const index::Slice& columns) const override {
        return to_packed()->slice_inner(rows, columns);
    }

    std::shared_ptr<const Content> slice_one_inner(std::ptrdiff_t row,
                                                   const index::Slice& columns) const override {
        return to_packed()->slice_one_inner(row, columns);
    }

    std::shared_ptr<const Content> take_inner(const std::vector<std::ptrdiff_t>& rows,
                                              std::ptrdiff_t column) const override {
        return to_packed()->take_inner(rows, column);
    }

    std::shared_ptr<const Content> take_inner_array(const std::vector<std::ptrdiff_t>& rows,
                                                    const std::vector<std::ptrdiff_t>& columns) const override {
        return to_packed()->take_inner_array(rows, columns);
    }

    std::shared_ptr<const Content> take_one_inner_array(
        std::ptrdiff_t row, const std::vector<std::ptrdiff_t>& columns) const override {
        return to_packed()->take_one_inner_array(row, columns);
    }

    std::shared_ptr<const Content> flatten() const override {
        return to_packed()->flatten();
    }

    std::shared_ptr<const Content> to_packed() const override {
        return content_->take_rows(index_)->to_packed();
    }

    std::shared_ptr<const Content> local_index() const override {
        std::vector<std::int64_t> values;
        values.reserve(length());
        for (std::size_t i = 0; i < length(); ++i) values.push_back(static_cast<std::int64_t>(i));
        return std::make_shared<NumpyArray<std::int64_t>>(std::move(values));
    }

    const std::vector<std::ptrdiff_t>& index() const noexcept { return index_; }
    const Content& content() const noexcept { return *content_; }

    Form to_buffers(detail::BufferBuilder& builder) const override {
        auto key = builder.next_key();
        builder.add(key + "-index", detail::index_buffer_from_signed(index_));
        auto content_form = content_->to_buffers(builder);
        return Form{
            .kind = FormKind::indexed,
            .key = std::move(key),
            .contents = {std::move(content_form)},
            .length = length(),
        };
    }

private:
    std::shared_ptr<const Content> select(const std::vector<std::size_t>& positions) const {
        std::vector<std::ptrdiff_t> index;
        index.reserve(positions.size());
        for (const auto position : positions) index.push_back(index_[position]);
        return std::make_shared<IndexedArray>(std::move(index), content_);
    }

    std::string validate() const {
        if (!content_) return "ak::IndexedArray content must not be null";
        const auto error = content_->validity_error();
        if (!error.empty()) return error;
        for (const auto item : index_) {
            if (item < 0 || static_cast<std::size_t>(item) >= content_->length()) {
                return "ak::IndexedArray index entries must refer to content";
            }
        }
        return {};
    }

    std::vector<std::ptrdiff_t> index_;
    std::shared_ptr<const Content> content_;
};

}  // namespace ak
