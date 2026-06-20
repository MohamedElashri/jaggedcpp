#pragma once

#include "awkward/contents/content.hpp"
#include "awkward/contents/detail.hpp"
#include "awkward/contents/list_offset_array.hpp"
#include "awkward/contents/numpy_array.hpp"

#include <algorithm>
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

template <typename T>
class RegularArray final : public Content {
public:
    using value_type = T;

    RegularArray(std::shared_ptr<const NumpyArray<T>> content,
                 std::size_t size,
                 std::optional<std::size_t> length = std::nullopt)
        : content_(std::move(content)),
          size_(size),
          length_(length.value_or(size == 0 || !content_ ? 0 : content_->length() / size)) {
        const auto error = validate();
        if (!error.empty()) {
            throw std::invalid_argument(error);
        }
    }

    RegularArray(std::vector<T> values, std::size_t size, std::optional<std::size_t> length = std::nullopt)
        : RegularArray(std::make_shared<NumpyArray<T>>(std::move(values)), size, length) {}

    LayoutKind kind() const noexcept override {
        return LayoutKind::regular;
    }

    std::size_t length() const noexcept override {
        return length_;
    }

    std::size_t nbytes() const noexcept override {
        return content_->nbytes();
    }

    std::string typestr() const override {
        return std::to_string(length()) + " * " + std::to_string(size_) + " * " +
               detail::primitive_type_name<T>();
    }

    std::string validity_error() const override {
        return validate();
    }

    Value to_list() const override {
        Value::list_type rows;
        rows.reserve(length());
        const auto& values = content_->values_vector();
        for (std::size_t row = 0; row < length(); ++row) {
            Value::list_type items;
            items.reserve(size_);
            const auto start = row * size_;
            for (std::size_t i = 0; i < size_; ++i) {
                items.emplace_back(detail::scalar_to_value(values[start + i]));
            }
            rows.emplace_back(std::move(items));
        }
        return rows;
    }

    Value at(std::ptrdiff_t index) const override {
        return as_list_offset().at(index);
    }

    Value at(std::ptrdiff_t outer, std::ptrdiff_t inner) const override {
        const auto row = index::detail::normalize_integer(outer, length(), "row");
        const auto column = index::detail::normalize_integer(inner, size_, "column");
        return detail::scalar_to_value(content_->values_vector()[row * size_ + column]);
    }

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

    std::shared_ptr<const Content> slice_inner(const index::Slice& rows, const index::Slice& columns) const override {
        return as_list_offset().slice_inner(rows, columns);
    }

    std::shared_ptr<const Content> slice_one_inner(std::ptrdiff_t row, const index::Slice& columns) const override {
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

    std::shared_ptr<const Content> slice_items(const std::vector<index::Item>& items) const {
        return as_list_offset().slice_items(items);
    }

    std::vector<std::size_t> num() const override {
        return std::vector<std::size_t>(length(), size_);
    }

    std::shared_ptr<const Content> flatten() const override {
        return content_;
    }

    std::shared_ptr<const Content> to_packed() const override {
        return std::make_shared<RegularArray<T>>(content_, size_, length_);
    }

    std::shared_ptr<const Content> local_index() const override {
        std::vector<std::int64_t> values;
        values.reserve(content_->length());
        for (std::size_t row = 0; row < length(); ++row) {
            for (std::size_t i = 0; i < size_; ++i) {
                values.push_back(static_cast<std::int64_t>(i));
            }
        }
        return std::make_shared<RegularArray<std::int64_t>>(std::move(values), size_, length_);
    }

    std::size_t size() const noexcept {
        return size_;
    }

    const NumpyArray<T>& content() const noexcept {
        return *content_;
    }

    Form to_buffers(detail::BufferBuilder& builder) const override {
        auto key = builder.next_key();
        auto content_form = content_->to_buffers(builder);
        return Form{
            .kind = FormKind::regular,
            .key = std::move(key),
            .contents = {std::move(content_form)},
            .length = length(),
            .size = size_,
        };
    }

private:
    ListOffsetArray<T> as_list_offset() const {
        std::vector<std::size_t> offsets;
        offsets.reserve(length() + 1);
        offsets.push_back(0);
        for (std::size_t row = 0; row < length(); ++row) {
            offsets.push_back((row + 1) * size_);
        }
        return ListOffsetArray<T>(content_, std::move(offsets));
    }

    std::string validate() const {
        if (!content_) {
            return "ak::RegularArray content must not be null";
        }
        if (size_ == 0) {
            return content_->length() == 0 ? std::string{} :
                                             "ak::RegularArray size zero requires empty content";
        }
        if (length_ > std::numeric_limits<std::size_t>::max() / size_ || length_ * size_ != content_->length()) {
            return "ak::RegularArray content length must equal length times regular size";
        }
        return {};
    }

    std::shared_ptr<const NumpyArray<T>> content_;
    std::size_t size_;
    std::size_t length_;
};

}  // namespace ak
