#pragma once

#include "awkward/contents/content.hpp"
#include "awkward/contents/detail.hpp"

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace ak {

template <typename T>
class ListOffsetArray;

template <typename T>
class NumpyArray final : public Content {
public:
    using value_type = T;

    NumpyArray() = default;
    explicit NumpyArray(std::vector<T> values) : values_(std::move(values)) {}

    LayoutKind kind() const noexcept override {
        return LayoutKind::numpy;
    }

    std::size_t length() const noexcept override {
        return values_.size();
    }

    std::size_t nbytes() const noexcept override {
        return detail::primitive_nbytes(values_);
    }

    std::string typestr() const override {
        return std::to_string(length()) + " * " + detail::primitive_type_name<T>();
    }

    std::string validity_error() const override {
        return {};
    }

    Value to_list() const override {
        return detail::vector_to_value(values_);
    }

    const std::vector<bool>* flat_bool_mask() const noexcept override {
        if constexpr (std::same_as<T, bool>) {
            return &values_;
        } else {
            return nullptr;
        }
    }

    Value at(std::ptrdiff_t index) const override {
        return detail::scalar_to_value(values_[index::detail::normalize_integer(index, length(), "array")]);
    }

    std::shared_ptr<const Content> slice_rows(const index::Slice& slice) const override {
        return take(index::detail::indices_for_slice(slice, length()));
    }

    std::shared_ptr<const Content> take_rows(const std::vector<std::ptrdiff_t>& indices) const override {
        return take(index::detail::normalize_integer_array(indices, length(), "array"));
    }

    std::shared_ptr<const Content> mask_rows(const std::vector<bool>& mask) const override {
        return take_mask(mask);
    }

    std::shared_ptr<const Content> mask_as_array(const Content& mask) const override {
        const auto* flat_mask = mask.flat_bool_mask();
        if (flat_mask == nullptr) {
            throw std::invalid_argument("primitive array indexing requires a flat boolean mask");
        }
        return take_mask(*flat_mask);
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
        if (normalized_items.size() != 1) {
            throw std::invalid_argument("primitive arrays only support one dimension index");
        }

        const auto& item = normalized_items.front();
        if (const auto* integer = std::get_if<index::Integer>(&item)) {
            return std::make_shared<NumpyArray<T>>(
                std::vector<T>{values_[index::detail::normalize_integer(integer->value, length(), "array")]});
        }
        if (const auto* slice = std::get_if<index::Slice>(&item)) {
            return take(index::detail::indices_for_slice(*slice, length()));
        }
        if (const auto* integers = std::get_if<index::IntegerArray>(&item)) {
            return take(index::detail::normalize_integer_array(integers->values, length(), "array"));
        }
        if (const auto* booleans = std::get_if<index::BooleanArray>(&item)) {
            return take_mask(booleans->values);
        }
        if (const auto* array_index = std::get_if<index::ArrayIndex>(&item)) {
            const auto* mask = array_index->layout->flat_bool_mask();
            if (mask == nullptr) {
                throw std::invalid_argument("primitive array indexing requires a flat boolean mask");
            }
            return take_mask(*mask);
        }
        throw std::invalid_argument("newaxis and field indexing require later compatibility phases");
    }

    std::shared_ptr<const Content> unflatten(const std::vector<std::size_t>& counts) const override;

    std::shared_ptr<const Content> to_packed() const override {
        return std::make_shared<NumpyArray<T>>(values_);
    }

    std::shared_ptr<const Content> local_index() const override {
        std::vector<std::int64_t> values;
        values.reserve(length());
        for (std::size_t i = 0; i < length(); ++i) {
            values.push_back(static_cast<std::int64_t>(i));
        }
        return std::make_shared<NumpyArray<std::int64_t>>(std::move(values));
    }

    std::span<const T> values() const noexcept requires (!std::same_as<T, bool>) {
        return std::span<const T>(values_.data(), values_.size());
    }

    const std::vector<T>& values_vector() const noexcept {
        return values_;
    }

    Form to_buffers(detail::BufferBuilder& builder) const override {
        if constexpr (detail::is_string_like_v<T>) {
            std::vector<std::size_t> offsets;
            std::vector<std::uint8_t> bytes;
            offsets.reserve(values_.size() + 1);
            offsets.push_back(0);
            for (const auto& value : values_) {
                bytes.insert(bytes.end(), value.begin(), value.end());
                offsets.push_back(bytes.size());
            }

            auto key = builder.next_key();
            builder.add(key + "-offsets", detail::index_buffer_from_offsets(offsets));
            auto content_key = builder.next_key();
            builder.add(content_key + "-data", std::move(bytes));
            Form content{
                .kind = FormKind::numpy,
                .key = std::move(content_key),
                .primitive = "uint8",
                .length = offsets.back(),
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
        auto key = builder.next_key();
        builder.add(key + "-data", detail::primitive_buffer_from_vector(values_));
        return Form{
            .kind = FormKind::numpy,
            .key = std::move(key),
            .primitive = detail::primitive_type_name<T>(),
            .length = length(),
        };
    }

private:
    std::shared_ptr<const Content> take(const std::vector<std::size_t>& indices) const {
        std::vector<T> result;
        result.reserve(indices.size());
        for (const auto index : indices) {
            result.push_back(values_[index]);
        }
        return std::make_shared<NumpyArray<T>>(std::move(result));
    }

    std::shared_ptr<const Content> take_mask(const std::vector<bool>& mask) const {
        if (mask.size() != length()) {
            throw std::invalid_argument("boolean mask length must match array length");
        }
        std::vector<T> result;
        for (std::size_t i = 0; i < length(); ++i) {
            if (mask[i]) {
                result.push_back(values_[i]);
            }
        }
        return std::make_shared<NumpyArray<T>>(std::move(result));
    }

    std::vector<T> values_;
};

}  // namespace ak
