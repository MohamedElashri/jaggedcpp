#pragma once

#include "awkward/buffers.hpp"
#include "awkward/index.hpp"
#include "awkward/value.hpp"

#include <cstddef>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace ak {

enum class LayoutKind {
    empty,
    numpy,
    string,
    list,
    list_offset,
    regular,
    indexed,
    indexed_option,
    byte_masked,
    bit_masked,
    unmasked,
    list_offset_content,
    record,
    union_,
};

class Content {
public:
    virtual ~Content() = default;

    virtual LayoutKind kind() const noexcept = 0;
    virtual std::size_t length() const noexcept = 0;
    virtual std::size_t nbytes() const noexcept = 0;
    virtual std::string typestr() const = 0;
    virtual std::string validity_error() const = 0;
    virtual Value to_list() const = 0;

    virtual const std::vector<bool>* flat_bool_mask() const noexcept {
        return nullptr;
    }

    virtual bool ragged_bool_mask(std::vector<bool>& values, std::vector<std::size_t>& offsets) const {
        (void)values;
        (void)offsets;
        return false;
    }

    virtual Value at(std::ptrdiff_t index) const {
        (void)index;
        throw std::out_of_range("layout does not support scalar indexing");
    }

    virtual Value at(std::ptrdiff_t outer, std::ptrdiff_t inner) const {
        (void)outer;
        (void)inner;
        throw std::out_of_range("layout does not support nested indexing");
    }

    virtual std::shared_ptr<const Content> getitem(const std::vector<index::Item>& items) const {
        (void)items;
        throw std::invalid_argument("layout does not support slicing");
    }

    virtual std::shared_ptr<const Content> slice_rows(const index::Slice& slice) const {
        (void)slice;
        throw std::invalid_argument("layout does not support row slicing");
    }

    virtual std::shared_ptr<const Content> take_rows(const std::vector<std::ptrdiff_t>& indices) const {
        (void)indices;
        throw std::invalid_argument("layout does not support row indexing");
    }

    virtual std::shared_ptr<const Content> mask_rows(const std::vector<bool>& mask) const {
        (void)mask;
        throw std::invalid_argument("layout does not support row masking");
    }

    virtual std::shared_ptr<const Content> mask_as_array(const Content& mask) const {
        (void)mask;
        throw std::invalid_argument("layout does not support array masking");
    }

    virtual std::vector<std::string> fields() const {
        return {};
    }

    virtual bool is_tuple() const noexcept {
        return false;
    }

    virtual std::string record_name() const {
        return {};
    }

    virtual std::shared_ptr<const Content> field(const std::string& name) const {
        (void)name;
        throw std::invalid_argument("layout does not support field projection");
    }

    virtual std::shared_ptr<const Content> project_fields(const std::vector<std::string>& names) const {
        (void)names;
        throw std::invalid_argument("layout does not support field projection");
    }

    virtual std::shared_ptr<const Content> with_name(std::string name) const {
        (void)name;
        throw std::invalid_argument("layout does not support record names");
    }

    virtual std::shared_ptr<const Content> slice_inner(const index::Slice& rows, const index::Slice& columns) const {
        (void)rows;
        (void)columns;
        throw std::invalid_argument("layout does not support nested slicing");
    }

    virtual std::shared_ptr<const Content> slice_one_inner(std::ptrdiff_t row, const index::Slice& columns) const {
        (void)row;
        (void)columns;
        throw std::invalid_argument("layout does not support nested slicing");
    }

    virtual std::shared_ptr<const Content> take_inner(const std::vector<std::ptrdiff_t>& rows,
                                                      std::ptrdiff_t column) const {
        (void)rows;
        (void)column;
        throw std::invalid_argument("layout does not support nested indexing");
    }

    virtual std::shared_ptr<const Content> take_inner_array(const std::vector<std::ptrdiff_t>& rows,
                                                            const std::vector<std::ptrdiff_t>& columns) const {
        (void)rows;
        (void)columns;
        throw std::invalid_argument("layout does not support nested integer-array indexing");
    }

    virtual std::shared_ptr<const Content> take_one_inner_array(
        std::ptrdiff_t row, const std::vector<std::ptrdiff_t>& columns) const {
        (void)row;
        (void)columns;
        throw std::invalid_argument("layout does not support nested integer-array indexing");
    }

    virtual std::vector<std::size_t> num() const {
        throw std::invalid_argument("ak::num is only available for list-like layouts");
    }

    virtual std::shared_ptr<const Content> flatten() const {
        throw std::invalid_argument("ak::flatten is only available for list-like layouts");
    }

    virtual std::shared_ptr<const Content> unflatten(const std::vector<std::size_t>& counts) const {
        (void)counts;
        throw std::invalid_argument("ak::unflatten is only available for primitive layouts");
    }

    virtual std::shared_ptr<const Content> to_packed() const = 0;

    virtual std::shared_ptr<const Content> local_index() const = 0;

    virtual Form to_buffers(detail::BufferBuilder& builder) const {
        (void)builder;
        throw std::invalid_argument("layout does not support buffer serialization");
    }

};

}  // namespace ak
