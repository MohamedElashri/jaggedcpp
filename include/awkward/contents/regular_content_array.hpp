#pragma once

#include "awkward/contents/content.hpp"
#include "awkward/contents/list_offset_content_array.hpp"
#include "awkward/contents/numpy_array.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace ak {

class RegularContentArray final : public Content {
public:
    RegularContentArray(std::shared_ptr<const Content> content,
                        std::size_t size,
                        std::optional<std::size_t> length = std::nullopt)
        : content_(std::move(content)),
          size_(size),
          length_(length.value_or(size == 0 || !content_ ? 0 : content_->length() / size)) {
        const auto error = validate();
        if (!error.empty()) throw std::invalid_argument(error);
    }

    LayoutKind kind() const noexcept override { return LayoutKind::regular; }
    std::size_t length() const noexcept override { return length_; }
    std::size_t nbytes() const noexcept override { return content_->nbytes(); }

    std::string typestr() const override {
        return std::to_string(length_) + " * " + std::to_string(size_) + " * " +
               detail::item_type_from_typestr(content_->typestr());
    }

    std::string validity_error() const override { return validate(); }

    Value to_list() const override { return as_list_offset().to_list(); }
    Value at(std::ptrdiff_t index) const override { return as_list_offset().at(index); }
    Value at(std::ptrdiff_t outer, std::ptrdiff_t inner) const override { return as_list_offset().at(outer, inner); }

    std::shared_ptr<const Content> slice_rows(const index::Slice& slice) const override {
        return as_list_offset().slice_rows(slice);
    }
    std::shared_ptr<const Content> take_rows(const std::vector<std::ptrdiff_t>& indices) const override {
        return as_list_offset().take_rows(indices);
    }
    std::shared_ptr<const Content> mask_rows(const std::vector<bool>& mask) const override {
        return as_list_offset().mask_rows(mask);
    }
    std::shared_ptr<const Content> mask_as_array(const Content& mask) const override {
        return as_list_offset().mask_as_array(mask);
    }
    std::shared_ptr<const Content> slice_inner(const index::Slice& rows,
                                               const index::Slice& columns) const override {
        return as_list_offset().slice_inner(rows, columns);
    }
    std::shared_ptr<const Content> slice_one_inner(std::ptrdiff_t row,
                                                   const index::Slice& columns) const override {
        return as_list_offset().slice_one_inner(row, columns);
    }
    std::shared_ptr<const Content> take_inner(const std::vector<std::ptrdiff_t>& rows,
                                              std::ptrdiff_t column) const override {
        return as_list_offset().take_inner(rows, column);
    }
    std::shared_ptr<const Content> take_inner_array(const std::vector<std::ptrdiff_t>& rows,
                                                    const std::vector<std::ptrdiff_t>& columns) const override {
        return as_list_offset().take_inner_array(rows, columns);
    }
    std::shared_ptr<const Content> take_one_inner_array(
        std::ptrdiff_t row, const std::vector<std::ptrdiff_t>& columns) const override {
        return as_list_offset().take_one_inner_array(row, columns);
    }

    std::vector<std::string> fields() const override { return content_->fields(); }
    bool is_tuple() const noexcept override { return content_->is_tuple(); }
    std::string record_name() const override { return content_->record_name(); }
    std::shared_ptr<const Content> field(const std::string& name) const override {
        return std::make_shared<RegularContentArray>(content_->field(name), size_, length_);
    }
    std::shared_ptr<const Content> project_fields(const std::vector<std::string>& names) const override {
        return std::make_shared<RegularContentArray>(content_->project_fields(names), size_, length_);
    }
    std::shared_ptr<const Content> with_name(std::string name) const override {
        return std::make_shared<RegularContentArray>(content_->with_name(std::move(name)), size_, length_);
    }

    std::vector<std::size_t> num() const override { return std::vector<std::size_t>(length_, size_); }
    std::shared_ptr<const Content> flatten() const override { return content_; }

    std::shared_ptr<const Content> to_packed() const override {
        return std::make_shared<RegularContentArray>(content_->to_packed(), size_, length_);
    }

    std::shared_ptr<const Content> local_index() const override {
        std::vector<std::int64_t> values;
        values.reserve(content_->length());
        for (std::size_t row = 0; row < length_; ++row) {
            for (std::size_t i = 0; i < size_; ++i) values.push_back(static_cast<std::int64_t>(i));
        }
        return std::make_shared<RegularContentArray>(
            std::make_shared<NumpyArray<std::int64_t>>(std::move(values)), size_, length_);
    }

    std::size_t size() const noexcept { return size_; }
    const Content& content() const noexcept { return *content_; }

    Form to_buffers(detail::BufferBuilder& builder) const override {
        auto key = builder.next_key();
        auto content_form = content_->to_buffers(builder);
        return Form{
            .kind = FormKind::regular,
            .key = std::move(key),
            .contents = {std::move(content_form)},
            .length = length_,
            .size = size_,
        };
    }

private:
    ListOffsetContentArray as_list_offset() const {
        std::vector<std::size_t> offsets;
        offsets.reserve(length_ + 1);
        for (std::size_t row = 0; row <= length_; ++row) offsets.push_back(row * size_);
        return ListOffsetContentArray(content_, std::move(offsets));
    }

    std::string validate() const {
        if (!content_) return "ak::RegularContentArray content must not be null";
        const auto error = content_->validity_error();
        if (!error.empty()) return error;
        if (size_ == 0) return content_->length() == 0 ? std::string{} :
                                                           "ak::RegularContentArray size zero requires empty content";
        if (length_ > std::numeric_limits<std::size_t>::max() / size_ || length_ * size_ != content_->length()) {
            return "ak::RegularContentArray content length must equal length times regular size";
        }
        return {};
    }

    std::shared_ptr<const Content> content_;
    std::size_t size_;
    std::size_t length_;
};

}  // namespace ak
