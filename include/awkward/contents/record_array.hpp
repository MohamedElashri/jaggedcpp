#pragma once

#include "awkward/contents/content.hpp"
#include "awkward/contents/detail.hpp"
#include "awkward/contents/numpy_array.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace ak {

class RecordArray final : public Content {
public:
    RecordArray(std::vector<std::string> fields,
                std::vector<std::shared_ptr<const Content>> contents,
                bool is_tuple = false,
                std::string record_name = {},
                std::optional<std::size_t> length = std::nullopt)
        : fields_(std::move(fields)),
          contents_(std::move(contents)),
          is_tuple_(is_tuple),
          record_name_(std::move(record_name)),
          length_(length.value_or(contents_.empty() ? 0 : contents_.front()->length())) {
        const auto error = validate();
        if (!error.empty()) {
            throw std::invalid_argument(error);
        }
    }

    LayoutKind kind() const noexcept override {
        return LayoutKind::record;
    }

    std::size_t length() const noexcept override {
        return length_;
    }

    std::size_t nbytes() const noexcept override {
        std::size_t total = 0;
        for (const auto& content : contents_) {
            total += content->nbytes();
        }
        return total;
    }

    std::string typestr() const override {
        std::string item;
        item += is_tuple_ ? '(' : '{';
        for (std::size_t i = 0; i < contents_.size(); ++i) {
            if (i != 0) {
                item += ", ";
            }
            if (!is_tuple_) {
                item += fields_[i] + ": ";
            }
            item += detail::item_type_from_typestr(contents_[i]->typestr());
        }
        if (is_tuple_ && contents_.size() == 1) {
            item += ",";
        }
        item += is_tuple_ ? ')' : '}';
        if (!record_name_.empty()) {
            item = record_name_ + item;
        }
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

    Value at(std::ptrdiff_t index) const override {
        return value_at(index::detail::normalize_integer(index, length(), "record"));
    }

    std::shared_ptr<const Content> slice_rows(const index::Slice& slice) const override {
        return select(index::detail::indices_for_slice(slice, length()));
    }

    std::shared_ptr<const Content> take_rows(const std::vector<std::ptrdiff_t>& indices) const override {
        return select(index::detail::normalize_integer_array(indices, length(), "record"));
    }

    std::shared_ptr<const Content> mask_rows(const std::vector<bool>& mask) const override {
        if (mask.size() != length()) {
            throw std::invalid_argument("boolean mask length must match record array length");
        }
        std::vector<std::ptrdiff_t> indices;
        for (std::size_t i = 0; i < mask.size(); ++i) {
            if (mask[i]) {
                indices.push_back(static_cast<std::ptrdiff_t>(i));
            }
        }
        return take_rows(indices);
    }

    std::shared_ptr<const Content> mask_as_array(const Content& mask) const override {
        const auto* flat_mask = mask.flat_bool_mask();
        if (flat_mask == nullptr) {
            throw std::invalid_argument("record array indexing requires a flat boolean mask");
        }
        return mask_rows(*flat_mask);
    }

    std::vector<std::string> fields() const override {
        return fields_;
    }

    bool is_tuple() const noexcept override {
        return is_tuple_;
    }

    std::string record_name() const override {
        return record_name_;
    }

    std::shared_ptr<const Content> field(const std::string& name) const override {
        return contents_[field_index(name)];
    }

    std::shared_ptr<const Content> project_fields(const std::vector<std::string>& names) const override {
        std::vector<std::string> fields;
        std::vector<std::shared_ptr<const Content>> contents;
        fields.reserve(names.size());
        contents.reserve(names.size());
        for (const auto& name : names) {
            const auto index = field_index(name);
            fields.push_back(fields_[index]);
            contents.push_back(contents_[index]);
        }
        return std::make_shared<RecordArray>(std::move(fields), std::move(contents), is_tuple_, record_name_, length_);
    }

    std::shared_ptr<const Content> with_name(std::string name) const override {
        return std::make_shared<RecordArray>(fields_, contents_, is_tuple_, std::move(name), length_);
    }

    std::shared_ptr<const Content> to_packed() const override {
        std::vector<std::shared_ptr<const Content>> contents;
        contents.reserve(contents_.size());
        for (const auto& content : contents_) {
            contents.push_back(content->to_packed());
        }
        return std::make_shared<RecordArray>(fields_, std::move(contents), is_tuple_, record_name_, length_);
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
        std::vector<Form> content_forms;
        content_forms.reserve(contents_.size());
        for (const auto& content : contents_) {
            content_forms.push_back(content->to_buffers(builder));
        }
        return Form{
            .kind = FormKind::record,
            .key = std::move(key),
            .contents = std::move(content_forms),
            .fields = fields_,
            .is_tuple = is_tuple_,
            .record_name = record_name_,
            .length = length(),
        };
    }

private:
    std::size_t field_index(const std::string& name) const {
        const auto found = std::find(fields_.begin(), fields_.end(), name);
        if (found == fields_.end()) {
            throw std::out_of_range("record field does not exist: " + name);
        }
        return static_cast<std::size_t>(std::distance(fields_.begin(), found));
    }

    Value value_at(std::size_t index) const {
        Value::record_type record;
        record.is_tuple = is_tuple_;
        record.fields = fields_;
        record.values.reserve(contents_.size());
        for (const auto& content : contents_) {
            record.values.push_back(content->at(static_cast<std::ptrdiff_t>(index)));
        }
        return record;
    }

    std::shared_ptr<const Content> select(const std::vector<std::size_t>& indices) const {
        std::vector<std::ptrdiff_t> signed_indices;
        signed_indices.reserve(indices.size());
        for (const auto index : indices) {
            signed_indices.push_back(static_cast<std::ptrdiff_t>(index));
        }

        std::vector<std::shared_ptr<const Content>> contents;
        contents.reserve(contents_.size());
        for (const auto& content : contents_) {
            contents.push_back(content->take_rows(signed_indices));
        }
        return std::make_shared<RecordArray>(fields_, std::move(contents), is_tuple_, record_name_, indices.size());
    }

    std::string validate() const {
        if (fields_.size() != contents_.size()) {
            return "ak::RecordArray fields and contents must have the same length";
        }
        for (std::size_t i = 0; i < fields_.size(); ++i) {
            if (fields_[i].empty()) {
                return "ak::RecordArray field names must not be empty";
            }
            if (std::find(fields_.begin(), fields_.begin() + static_cast<std::ptrdiff_t>(i), fields_[i]) !=
                fields_.begin() + static_cast<std::ptrdiff_t>(i)) {
                return "ak::RecordArray field names must be unique";
            }
            if (!contents_[i]) {
                return "ak::RecordArray contents must not be null";
            }
            const auto content_error = contents_[i]->validity_error();
            if (!content_error.empty()) {
                return content_error;
            }
            if (i != 0 && contents_[i]->length() != contents_.front()->length()) {
                return "ak::RecordArray contents must have equal lengths";
            }
            if (contents_[i]->length() != length_) {
                return "ak::RecordArray contents must match record array length";
            }
        }
        return {};
    }

    std::vector<std::string> fields_;
    std::vector<std::shared_ptr<const Content>> contents_;
    bool is_tuple_;
    std::string record_name_;
    std::size_t length_;
};

}  // namespace ak
