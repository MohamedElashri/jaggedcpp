#pragma once

#include "awkward/contents/content.hpp"
#include "awkward/contents/detail.hpp"
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

class IndexedOptionArray final : public Content {
public:
    IndexedOptionArray(std::vector<std::ptrdiff_t> index, std::shared_ptr<const Content> content)
        : index_(std::move(index)), content_(std::move(content)) {
        const auto error = validate();
        if (!error.empty()) {
            throw std::invalid_argument(error);
        }
    }

    LayoutKind kind() const noexcept override {
        return LayoutKind::indexed_option;
    }

    std::size_t length() const noexcept override {
        return index_.size();
    }

    std::size_t nbytes() const noexcept override {
        return index_.size() * sizeof(std::ptrdiff_t) + content_->nbytes();
    }

    std::string typestr() const override {
        return std::to_string(length()) + " * ?" + detail::item_type_from_typestr(content_->typestr());
    }

    std::string validity_error() const override {
        return validate();
    }

    Value to_list() const override {
        Value::list_type values;
        values.reserve(length());
        for (std::size_t i = 0; i < length(); ++i) {
            values.emplace_back(value_at(i));
        }
        return values;
    }

    Value at(std::ptrdiff_t index) const override {
        return value_at(index::detail::normalize_integer(index, length(), "array"));
    }

    std::shared_ptr<const Content> slice_rows(const index::Slice& slice) const override {
        return take(index::detail::indices_for_slice(slice, length()));
    }

    std::shared_ptr<const Content> take_rows(const std::vector<std::ptrdiff_t>& indices) const override {
        return take(index::detail::normalize_integer_array(indices, length(), "array"));
    }

    std::shared_ptr<const Content> mask_rows(const std::vector<bool>& mask) const override {
        if (mask.size() != length()) {
            throw std::invalid_argument("boolean mask length must match array length");
        }
        std::vector<std::ptrdiff_t> index;
        for (std::size_t i = 0; i < mask.size(); ++i) {
            if (mask[i]) {
                index.push_back(index_[i]);
            }
        }
        return std::make_shared<IndexedOptionArray>(std::move(index), content_);
    }

    std::shared_ptr<const Content> mask_as_array(const Content& mask) const override {
        const auto* flat_mask = mask.flat_bool_mask();
        if (flat_mask == nullptr) {
            throw std::invalid_argument("option array indexing requires a flat boolean mask");
        }
        return mask_rows(*flat_mask);
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
        return std::make_shared<IndexedOptionArray>(index_, content_->field(name));
    }

    std::shared_ptr<const Content> project_fields(const std::vector<std::string>& names) const override {
        return std::make_shared<IndexedOptionArray>(index_, content_->project_fields(names));
    }

    std::shared_ptr<const Content> with_name(std::string name) const override {
        return std::make_shared<IndexedOptionArray>(index_, content_->with_name(std::move(name)));
    }

    std::shared_ptr<const Content> to_packed() const override {
        std::vector<std::ptrdiff_t> packed_index;
        std::vector<std::ptrdiff_t> content_rows;
        packed_index.reserve(index_.size());
        content_rows.reserve(index_.size());
        for (const auto item : index_) {
            if (item < 0) {
                packed_index.push_back(-1);
                continue;
            }
            packed_index.push_back(static_cast<std::ptrdiff_t>(content_rows.size()));
            content_rows.push_back(item);
        }
        return std::make_shared<IndexedOptionArray>(std::move(packed_index), content_->take_rows(content_rows));
    }

    std::shared_ptr<const Content> local_index() const override {
        std::vector<std::int64_t> values;
        values.reserve(length());
        for (std::size_t i = 0; i < length(); ++i) {
            values.push_back(static_cast<std::int64_t>(i));
        }
        return std::make_shared<NumpyArray<std::int64_t>>(std::move(values));
    }

    const std::vector<std::ptrdiff_t>& index() const noexcept {
        return index_;
    }

    const Content& content() const noexcept {
        return *content_;
    }

    Form to_buffers(detail::BufferBuilder& builder) const override {
        auto key = builder.next_key();
        builder.add(key + "-index", detail::index_buffer_from_signed(index_));
        auto content_form = content_->to_buffers(builder);
        return Form{
            .kind = FormKind::indexed_option,
            .key = std::move(key),
            .contents = {std::move(content_form)},
            .length = length(),
        };
    }

private:
    Value value_at(std::size_t position) const {
        if (index_[position] < 0) {
            return Value(nullptr);
        }
        return content_->at(index_[position]);
    }

    std::shared_ptr<const Content> take(const std::vector<std::size_t>& indices) const {
        std::vector<std::ptrdiff_t> index;
        index.reserve(indices.size());
        for (const auto i : indices) {
            index.push_back(index_[i]);
        }
        return std::make_shared<IndexedOptionArray>(std::move(index), content_);
    }

    std::string validate() const {
        if (!content_) {
            return "ak::IndexedOptionArray content must not be null";
        }
        const auto content_error = content_->validity_error();
        if (!content_error.empty()) {
            return content_error;
        }
        for (const auto item : index_) {
            if (item >= 0 && static_cast<std::size_t>(item) >= content_->length()) {
                return "ak::IndexedOptionArray index entries must refer to content or be negative";
            }
        }
        return {};
    }

    std::vector<std::ptrdiff_t> index_;
    std::shared_ptr<const Content> content_;
};

class ByteMaskedArray final : public Content {
public:
    ByteMaskedArray(std::vector<std::uint8_t> mask, std::shared_ptr<const Content> content, bool valid_when = true)
        : mask_(std::move(mask)), content_(std::move(content)), valid_when_(valid_when) {
        const auto error = validate();
        if (!error.empty()) {
            throw std::invalid_argument(error);
        }
    }

    LayoutKind kind() const noexcept override {
        return LayoutKind::byte_masked;
    }

    std::size_t length() const noexcept override {
        return mask_.size();
    }

    std::size_t nbytes() const noexcept override {
        return mask_.size() * sizeof(std::uint8_t) + content_->nbytes();
    }

    std::string typestr() const override {
        return std::to_string(length()) + " * ?" + detail::item_type_from_typestr(content_->typestr());
    }

    std::string validity_error() const override {
        return validate();
    }

    Value to_list() const override {
        Value::list_type values;
        values.reserve(length());
        for (std::size_t i = 0; i < length(); ++i) {
            values.emplace_back(value_at(i));
        }
        return values;
    }

    Value at(std::ptrdiff_t index) const override {
        return value_at(index::detail::normalize_integer(index, length(), "array"));
    }

    std::shared_ptr<const Content> slice_rows(const index::Slice& slice) const override {
        return as_indexed_option()->slice_rows(slice);
    }
    std::shared_ptr<const Content> take_rows(const std::vector<std::ptrdiff_t>& indices) const override {
        return as_indexed_option()->take_rows(indices);
    }
    std::shared_ptr<const Content> mask_rows(const std::vector<bool>& mask) const override {
        return as_indexed_option()->mask_rows(mask);
    }
    std::shared_ptr<const Content> mask_as_array(const Content& mask) const override {
        return as_indexed_option()->mask_as_array(mask);
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
        return std::make_shared<ByteMaskedArray>(mask_, content_->field(name), valid_when_);
    }

    std::shared_ptr<const Content> project_fields(const std::vector<std::string>& names) const override {
        return std::make_shared<ByteMaskedArray>(mask_, content_->project_fields(names), valid_when_);
    }

    std::shared_ptr<const Content> with_name(std::string name) const override {
        return std::make_shared<ByteMaskedArray>(mask_, content_->with_name(std::move(name)), valid_when_);
    }

    std::shared_ptr<const Content> to_packed() const override {
        std::vector<std::ptrdiff_t> content_rows;
        content_rows.reserve(length());
        for (std::size_t i = 0; i < length(); ++i) {
            content_rows.push_back(static_cast<std::ptrdiff_t>(i));
        }
        return std::make_shared<ByteMaskedArray>(mask_, content_->take_rows(content_rows), valid_when_);
    }

    std::shared_ptr<const Content> local_index() const override {
        std::vector<std::int64_t> values;
        values.reserve(length());
        for (std::size_t i = 0; i < length(); ++i) {
            values.push_back(static_cast<std::int64_t>(i));
        }
        return std::make_shared<NumpyArray<std::int64_t>>(std::move(values));
    }

    Form to_buffers(detail::BufferBuilder& builder) const override {
        auto key = builder.next_key();
        builder.add(key + "-mask", mask_);
        auto content_form = content_->to_buffers(builder);
        return Form{
            .kind = FormKind::byte_masked,
            .key = std::move(key),
            .contents = {std::move(content_form)},
            .length = length(),
            .valid_when = valid_when_,
        };
    }

private:
    std::shared_ptr<const IndexedOptionArray> as_indexed_option() const {
        std::vector<std::ptrdiff_t> index;
        index.reserve(length());
        for (std::size_t i = 0; i < length(); ++i) index.push_back(valid_at(i) ? static_cast<std::ptrdiff_t>(i) : -1);
        return std::make_shared<IndexedOptionArray>(std::move(index), content_);
    }

    bool valid_at(std::size_t position) const noexcept {
        return (mask_[position] != 0) == valid_when_;
    }

    Value value_at(std::size_t position) const {
        if (!valid_at(position)) {
            return Value(nullptr);
        }
        return content_->at(static_cast<std::ptrdiff_t>(position));
    }

    std::string validate() const {
        if (!content_) {
            return "ak::ByteMaskedArray content must not be null";
        }
        const auto content_error = content_->validity_error();
        if (!content_error.empty()) {
            return content_error;
        }
        if (content_->length() < mask_.size()) {
            return "ak::ByteMaskedArray content length must be at least mask length";
        }
        return {};
    }

    std::vector<std::uint8_t> mask_;
    std::shared_ptr<const Content> content_;
    bool valid_when_;
};

class BitMaskedArray final : public Content {
public:
    BitMaskedArray(std::vector<std::uint8_t> mask,
                   std::shared_ptr<const Content> content,
                   std::size_t length,
                   bool valid_when = true,
                   bool lsb_order = true)
        : mask_(std::move(mask)),
          content_(std::move(content)),
          length_(length),
          valid_when_(valid_when),
          lsb_order_(lsb_order) {
        const auto error = validate();
        if (!error.empty()) {
            throw std::invalid_argument(error);
        }
    }

    LayoutKind kind() const noexcept override {
        return LayoutKind::bit_masked;
    }

    std::size_t length() const noexcept override {
        return length_;
    }

    std::size_t nbytes() const noexcept override {
        return mask_.size() * sizeof(std::uint8_t) + content_->nbytes();
    }

    std::string typestr() const override {
        return std::to_string(length()) + " * ?" + detail::item_type_from_typestr(content_->typestr());
    }

    std::string validity_error() const override {
        return validate();
    }

    Value to_list() const override {
        Value::list_type values;
        values.reserve(length());
        for (std::size_t i = 0; i < length(); ++i) {
            values.emplace_back(value_at(i));
        }
        return values;
    }

    Value at(std::ptrdiff_t index) const override {
        return value_at(index::detail::normalize_integer(index, length(), "array"));
    }

    std::shared_ptr<const Content> slice_rows(const index::Slice& slice) const override {
        return as_indexed_option()->slice_rows(slice);
    }
    std::shared_ptr<const Content> take_rows(const std::vector<std::ptrdiff_t>& indices) const override {
        return as_indexed_option()->take_rows(indices);
    }
    std::shared_ptr<const Content> mask_rows(const std::vector<bool>& mask) const override {
        return as_indexed_option()->mask_rows(mask);
    }
    std::shared_ptr<const Content> mask_as_array(const Content& mask) const override {
        return as_indexed_option()->mask_as_array(mask);
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
        return std::make_shared<BitMaskedArray>(mask_, content_->field(name), length_, valid_when_, lsb_order_);
    }

    std::shared_ptr<const Content> project_fields(const std::vector<std::string>& names) const override {
        return std::make_shared<BitMaskedArray>(mask_, content_->project_fields(names), length_, valid_when_, lsb_order_);
    }

    std::shared_ptr<const Content> with_name(std::string name) const override {
        return std::make_shared<BitMaskedArray>(mask_, content_->with_name(std::move(name)), length_, valid_when_, lsb_order_);
    }

    std::shared_ptr<const Content> to_packed() const override {
        std::vector<std::ptrdiff_t> content_rows;
        content_rows.reserve(length());
        for (std::size_t i = 0; i < length(); ++i) {
            content_rows.push_back(static_cast<std::ptrdiff_t>(i));
        }
        return std::make_shared<BitMaskedArray>(mask_, content_->take_rows(content_rows), length_, valid_when_, lsb_order_);
    }

    std::shared_ptr<const Content> local_index() const override {
        std::vector<std::int64_t> values;
        values.reserve(length());
        for (std::size_t i = 0; i < length(); ++i) {
            values.push_back(static_cast<std::int64_t>(i));
        }
        return std::make_shared<NumpyArray<std::int64_t>>(std::move(values));
    }

    Form to_buffers(detail::BufferBuilder& builder) const override {
        auto key = builder.next_key();
        builder.add(key + "-mask", mask_);
        auto content_form = content_->to_buffers(builder);
        return Form{
            .kind = FormKind::bit_masked,
            .key = std::move(key),
            .contents = {std::move(content_form)},
            .length = length(),
            .valid_when = valid_when_,
            .lsb_order = lsb_order_,
        };
    }

private:
    std::shared_ptr<const IndexedOptionArray> as_indexed_option() const {
        std::vector<std::ptrdiff_t> index;
        index.reserve(length());
        for (std::size_t i = 0; i < length(); ++i) index.push_back(valid_at(i) ? static_cast<std::ptrdiff_t>(i) : -1);
        return std::make_shared<IndexedOptionArray>(std::move(index), content_);
    }

    bool valid_at(std::size_t position) const noexcept {
        const auto byte = mask_[position / 8];
        const auto bit = lsb_order_ ? position % 8 : 7 - position % 8;
        return (((byte >> bit) & 1U) != 0) == valid_when_;
    }

    Value value_at(std::size_t position) const {
        if (!valid_at(position)) {
            return Value(nullptr);
        }
        return content_->at(static_cast<std::ptrdiff_t>(position));
    }

    std::string validate() const {
        if (!content_) {
            return "ak::BitMaskedArray content must not be null";
        }
        const auto content_error = content_->validity_error();
        if (!content_error.empty()) {
            return content_error;
        }
        if (content_->length() < length_) {
            return "ak::BitMaskedArray content length must be at least masked length";
        }
        if (mask_.size() * 8 < length_) {
            return "ak::BitMaskedArray mask does not contain enough bits for length";
        }
        return {};
    }

    std::vector<std::uint8_t> mask_;
    std::shared_ptr<const Content> content_;
    std::size_t length_;
    bool valid_when_;
    bool lsb_order_;
};

class UnmaskedArray final : public Content {
public:
    explicit UnmaskedArray(std::shared_ptr<const Content> content) : content_(std::move(content)) {
        const auto error = validate();
        if (!error.empty()) {
            throw std::invalid_argument(error);
        }
    }

    LayoutKind kind() const noexcept override {
        return LayoutKind::unmasked;
    }

    std::size_t length() const noexcept override {
        return content_->length();
    }

    std::size_t nbytes() const noexcept override {
        return content_->nbytes();
    }

    std::string typestr() const override {
        return std::to_string(length()) + " * ?" + detail::item_type_from_typestr(content_->typestr());
    }

    std::string validity_error() const override {
        return validate();
    }

    Value to_list() const override {
        return content_->to_list();
    }

    Value at(std::ptrdiff_t index) const override {
        return content_->at(index);
    }

    std::shared_ptr<const Content> slice_rows(const index::Slice& slice) const override {
        return std::make_shared<UnmaskedArray>(content_->slice_rows(slice));
    }
    std::shared_ptr<const Content> take_rows(const std::vector<std::ptrdiff_t>& indices) const override {
        return std::make_shared<UnmaskedArray>(content_->take_rows(indices));
    }
    std::shared_ptr<const Content> mask_rows(const std::vector<bool>& mask) const override {
        return std::make_shared<UnmaskedArray>(content_->mask_rows(mask));
    }
    std::shared_ptr<const Content> mask_as_array(const Content& mask) const override {
        return std::make_shared<UnmaskedArray>(content_->mask_as_array(mask));
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
        return std::make_shared<UnmaskedArray>(content_->field(name));
    }

    std::shared_ptr<const Content> project_fields(const std::vector<std::string>& names) const override {
        return std::make_shared<UnmaskedArray>(content_->project_fields(names));
    }

    std::shared_ptr<const Content> with_name(std::string name) const override {
        return std::make_shared<UnmaskedArray>(content_->with_name(std::move(name)));
    }

    std::shared_ptr<const Content> to_packed() const override {
        return std::make_shared<UnmaskedArray>(content_->to_packed());
    }

    std::shared_ptr<const Content> local_index() const override {
        return content_->local_index();
    }

    Form to_buffers(detail::BufferBuilder& builder) const override {
        auto key = builder.next_key();
        auto content_form = content_->to_buffers(builder);
        return Form{
            .kind = FormKind::unmasked,
            .key = std::move(key),
            .contents = {std::move(content_form)},
            .length = length(),
        };
    }

private:
    std::string validate() const {
        if (!content_) {
            return "ak::UnmaskedArray content must not be null";
        }
        return content_->validity_error();
    }

    std::shared_ptr<const Content> content_;
};

}  // namespace ak
