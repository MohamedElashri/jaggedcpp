#pragma once

#include "awkward/contents/content.hpp"

#include <memory>
#include <string>

namespace ak {

class EmptyArray final : public Content {
public:
    LayoutKind kind() const noexcept override {
        return LayoutKind::empty;
    }

    std::size_t length() const noexcept override {
        return 0;
    }

    std::size_t nbytes() const noexcept override {
        return 0;
    }

    std::string typestr() const override {
        return "0 * unknown";
    }

    std::string validity_error() const override {
        return {};
    }

    Value to_list() const override {
        return Value::list_type{};
    }

    std::shared_ptr<const Content> getitem(const std::vector<index::Item>& items) const override {
        for (const auto& item : items) {
            if (std::holds_alternative<index::Ellipsis>(item)) {
                continue;
            }
            if (const auto* slice = std::get_if<index::Slice>(&item)) {
                if (index::detail::indices_for_slice(*slice, 0).empty()) {
                    return std::make_shared<EmptyArray>();
                }
            }
            throw std::out_of_range("empty array index is out of range");
        }
        return std::make_shared<EmptyArray>();
    }

    std::shared_ptr<const Content> slice_rows(const index::Slice& slice) const override {
        if (!index::detail::indices_for_slice(slice, 0).empty()) {
            throw std::out_of_range("empty array slice is out of range");
        }
        return std::make_shared<EmptyArray>();
    }

    std::shared_ptr<const Content> take_rows(const std::vector<std::ptrdiff_t>& indices) const override {
        if (!indices.empty()) {
            throw std::out_of_range("empty array index is out of range");
        }
        return std::make_shared<EmptyArray>();
    }

    std::shared_ptr<const Content> mask_rows(const std::vector<bool>& mask) const override {
        if (!mask.empty()) {
            throw std::invalid_argument("boolean mask length must match array length");
        }
        return std::make_shared<EmptyArray>();
    }

    std::shared_ptr<const Content> to_packed() const override {
        return std::make_shared<EmptyArray>();
    }

    std::shared_ptr<const Content> local_index() const override {
        return std::make_shared<EmptyArray>();
    }

    Form to_buffers(detail::BufferBuilder& builder) const override {
        return Form{
            .kind = FormKind::empty,
            .key = builder.next_key(),
            .length = length(),
        };
    }

};

}  // namespace ak
