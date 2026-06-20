#pragma once

#include "awkward/contents/content.hpp"
#include "awkward/contents/numpy_array.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace ak {

// Awkward strings are variable-length lists of UTF-8 bytes with string/char
// parameters. This node keeps that physical representation while presenting
// strings as scalar ak::Value objects through the high-level API.
class StringArray final : public Content {
public:
    explicit StringArray(const std::vector<std::string>& values) {
        offsets_.reserve(values.size() + 1);
        offsets_.push_back(0);
        for (const auto& value : values) {
            bytes_.insert(bytes_.end(), value.begin(), value.end());
            offsets_.push_back(bytes_.size());
        }
    }

    StringArray(std::vector<std::uint8_t> bytes, std::vector<std::size_t> offsets)
        : bytes_(std::move(bytes)), offsets_(std::move(offsets)) {
        const auto error = validate();
        if (!error.empty()) {
            throw std::invalid_argument(error);
        }
    }

    LayoutKind kind() const noexcept override {
        return LayoutKind::string;
    }

    std::size_t length() const noexcept override {
        return offsets_.size() - 1;
    }

    std::size_t nbytes() const noexcept override {
        return bytes_.size() + offsets_.size() * sizeof(std::int64_t);
    }

    std::string typestr() const override {
        return std::to_string(length()) + " * string";
    }

    std::string validity_error() const override {
        return validate();
    }

    Value to_list() const override {
        Value::list_type result;
        result.reserve(length());
        for (std::size_t i = 0; i < length(); ++i) {
            result.emplace_back(value_at(i));
        }
        return result;
    }

    Value at(std::ptrdiff_t index) const override {
        return Value(value_at(index::detail::normalize_integer(index, length(), "array")));
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
        std::vector<std::size_t> indices;
        for (std::size_t i = 0; i < mask.size(); ++i) {
            if (mask[i]) {
                indices.push_back(i);
            }
        }
        return take(indices);
    }

    std::shared_ptr<const Content> mask_as_array(const Content& mask) const override {
        const auto* values = mask.flat_bool_mask();
        if (values == nullptr) {
            throw std::invalid_argument("string array indexing requires a flat boolean mask");
        }
        return mask_rows(*values);
    }

    std::shared_ptr<const Content> to_packed() const override {
        return std::make_shared<StringArray>(bytes_, offsets_);
    }

    std::shared_ptr<const Content> local_index() const override {
        std::vector<std::int64_t> values;
        values.reserve(length());
        for (std::size_t i = 0; i < length(); ++i) {
            values.push_back(static_cast<std::int64_t>(i));
        }
        return std::make_shared<NumpyArray<std::int64_t>>(std::move(values));
    }

    std::span<const std::uint8_t> bytes() const noexcept {
        return bytes_;
    }

    std::span<const std::size_t> offsets() const noexcept {
        return offsets_;
    }

    std::vector<std::string> strings() const {
        std::vector<std::string> result;
        result.reserve(length());
        for (std::size_t i = 0; i < length(); ++i) {
            result.push_back(value_at(i));
        }
        return result;
    }

    Form to_buffers(detail::BufferBuilder& builder) const override {
        auto key = builder.next_key();
        builder.add(key + "-offsets", detail::index_buffer_from_offsets(offsets_));
        auto content_key = builder.next_key();
        builder.add(content_key + "-data", bytes_);
        Form content{
            .kind = FormKind::numpy,
            .key = std::move(content_key),
            .primitive = "uint8",
            .length = bytes_.size(),
            .parameters = {{"__array__", "char"}},
        };
        return Form{
            .kind = FormKind::list_offset,
            .key = std::move(key),
            .contents = {std::move(content)},
            .length = length(),
            .parameters = {{"__array__", "string"}},
        };
    }

private:
    std::string value_at(std::size_t index) const {
        const auto begin = bytes_.begin() + static_cast<std::ptrdiff_t>(offsets_[index]);
        const auto end = bytes_.begin() + static_cast<std::ptrdiff_t>(offsets_[index + 1]);
        return std::string(begin, end);
    }

    std::shared_ptr<const Content> take(const std::vector<std::size_t>& indices) const {
        std::vector<std::string> values;
        values.reserve(indices.size());
        for (const auto index : indices) {
            values.push_back(value_at(index));
        }
        return std::make_shared<StringArray>(values);
    }

    std::string validate() const {
        if (offsets_.empty()) {
            return "ak::StringArray offsets must contain at least the initial zero offset";
        }
        if (offsets_.front() != 0) {
            return "ak::StringArray offsets must start at zero";
        }
        if (!std::is_sorted(offsets_.begin(), offsets_.end())) {
            return "ak::StringArray offsets must be monotonic";
        }
        if (offsets_.back() != bytes_.size()) {
            return "ak::StringArray final offset must equal byte content length";
        }
        return {};
    }

    std::vector<std::uint8_t> bytes_;
    std::vector<std::size_t> offsets_;
};

}  // namespace ak
