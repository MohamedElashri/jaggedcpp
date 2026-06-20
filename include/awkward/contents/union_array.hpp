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

class UnionArray final : public Content {
public:
    UnionArray(std::vector<std::uint8_t> tags,
               std::vector<std::ptrdiff_t> index,
               std::vector<std::shared_ptr<const Content>> contents)
        : tags_(std::move(tags)), index_(std::move(index)), contents_(std::move(contents)) {
        const auto error = validate();
        if (!error.empty()) {
            throw std::invalid_argument(error);
        }
    }

    LayoutKind kind() const noexcept override {
        return LayoutKind::union_;
    }

    std::size_t length() const noexcept override {
        return tags_.size();
    }

    std::size_t nbytes() const noexcept override {
        std::size_t total = tags_.size() * sizeof(std::uint8_t) + index_.size() * sizeof(std::ptrdiff_t);
        for (const auto& content : contents_) {
            total += content->nbytes();
        }
        return total;
    }

    std::string typestr() const override {
        std::string item = "union[";
        for (std::size_t i = 0; i < contents_.size(); ++i) {
            if (i != 0) {
                item += ", ";
            }
            item += detail::item_type_from_typestr(contents_[i]->typestr());
        }
        item += ']';
        return std::to_string(length()) + " * " + item;
    }

    std::string validity_error() const override {
        return validate();
    }

    Value to_list() const override {
        Value::list_type values;
        values.reserve(length());
        for (std::size_t i = 0; i < length(); ++i) {
            values.push_back(value_at(i));
        }
        return values;
    }

    Value at(std::ptrdiff_t position) const override {
        return value_at(index::detail::normalize_integer(position, length(), "union"));
    }

    std::shared_ptr<const Content> slice_rows(const index::Slice& slice) const override {
        return select(index::detail::indices_for_slice(slice, length()));
    }

    std::shared_ptr<const Content> take_rows(const std::vector<std::ptrdiff_t>& indices) const override {
        return select(index::detail::normalize_integer_array(indices, length(), "union"));
    }

    std::shared_ptr<const Content> mask_rows(const std::vector<bool>& mask) const override {
        if (mask.size() != length()) {
            throw std::invalid_argument("boolean mask length must match union array length");
        }
        std::vector<std::size_t> indices;
        for (std::size_t i = 0; i < mask.size(); ++i) {
            if (mask[i]) {
                indices.push_back(i);
            }
        }
        return select(indices);
    }

    std::shared_ptr<const Content> mask_as_array(const Content& mask) const override {
        const auto* flat_mask = mask.flat_bool_mask();
        if (flat_mask == nullptr) {
            throw std::invalid_argument("union array indexing requires a flat boolean mask");
        }
        return mask_rows(*flat_mask);
    }

    std::shared_ptr<const Content> to_packed() const override {
        std::vector<std::vector<std::ptrdiff_t>> selected(contents_.size());
        std::vector<std::ptrdiff_t> packed_index;
        packed_index.reserve(index_.size());
        for (std::size_t i = 0; i < length(); ++i) {
            auto& content_indices = selected[tags_[i]];
            packed_index.push_back(static_cast<std::ptrdiff_t>(content_indices.size()));
            content_indices.push_back(index_[i]);
        }

        std::vector<std::shared_ptr<const Content>> packed_contents;
        packed_contents.reserve(contents_.size());
        for (std::size_t i = 0; i < contents_.size(); ++i) {
            packed_contents.push_back(contents_[i]->take_rows(selected[i]));
        }
        return std::make_shared<UnionArray>(tags_, std::move(packed_index), std::move(packed_contents));
    }

    std::shared_ptr<const Content> local_index() const override {
        std::vector<std::int64_t> values;
        values.reserve(length());
        for (std::size_t i = 0; i < length(); ++i) {
            values.push_back(static_cast<std::int64_t>(i));
        }
        return std::make_shared<NumpyArray<std::int64_t>>(std::move(values));
    }

    const std::vector<std::uint8_t>& tags() const noexcept {
        return tags_;
    }

    const std::vector<std::ptrdiff_t>& index() const noexcept {
        return index_;
    }

    const std::vector<std::shared_ptr<const Content>>& contents() const noexcept {
        return contents_;
    }

    Form to_buffers(detail::BufferBuilder& builder) const override {
        auto key = builder.next_key();
        builder.add(key + "-tags", tags_);
        builder.add(key + "-index", detail::index_buffer_from_signed(index_));
        std::vector<Form> content_forms;
        content_forms.reserve(contents_.size());
        for (const auto& content : contents_) {
            content_forms.push_back(content->to_buffers(builder));
        }
        return Form{
            .kind = FormKind::union_,
            .key = std::move(key),
            .contents = std::move(content_forms),
            .length = length(),
        };
    }

private:
    Value value_at(std::size_t position) const {
        return contents_[tags_[position]]->at(index_[position]);
    }

    std::shared_ptr<const Content> select(const std::vector<std::size_t>& indices) const {
        std::vector<std::uint8_t> tags;
        std::vector<std::ptrdiff_t> index;
        tags.reserve(indices.size());
        index.reserve(indices.size());
        for (const auto position : indices) {
            tags.push_back(tags_[position]);
            index.push_back(index_[position]);
        }
        return std::make_shared<UnionArray>(std::move(tags), std::move(index), contents_);
    }

    std::string validate() const {
        if (tags_.size() != index_.size()) {
            return "ak::UnionArray tags and index must have the same length";
        }
        if (contents_.empty()) {
            return "ak::UnionArray requires at least one content";
        }
        if (contents_.size() > 256) {
            return "ak::UnionArray supports at most 256 contents";
        }
        for (const auto& content : contents_) {
            if (!content) {
                return "ak::UnionArray contents must not be null";
            }
            const auto error = content->validity_error();
            if (!error.empty()) {
                return error;
            }
        }
        for (std::size_t i = 0; i < tags_.size(); ++i) {
            if (tags_[i] >= contents_.size()) {
                return "ak::UnionArray tag is outside the content range";
            }
            if (index_[i] < 0 || static_cast<std::size_t>(index_[i]) >= contents_[tags_[i]]->length()) {
                return "ak::UnionArray index is outside its selected content";
            }
        }
        return {};
    }

    std::vector<std::uint8_t> tags_;
    std::vector<std::ptrdiff_t> index_;
    std::vector<std::shared_ptr<const Content>> contents_;
};

}  // namespace ak
