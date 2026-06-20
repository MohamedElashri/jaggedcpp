#pragma once

#include "awkward/buffers.hpp"
#include "awkward/value.hpp"

#include <algorithm>
#include <cstddef>
#include <memory>
#include <ostream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace ak {

enum class TypeKind { unknown, numpy, list, regular, option, record, union_, array, scalar };

class Type {
public:
    virtual ~Type() = default;
    virtual TypeKind kind() const noexcept = 0;
    virtual std::string typestr() const = 0;
    virtual std::size_t ndim() const noexcept = 0;
};

using TypePtr = std::shared_ptr<const Type>;

inline bool operator==(const Type& left, const Type& right) {
    return left.kind() == right.kind() && left.typestr() == right.typestr();
}

inline std::ostream& operator<<(std::ostream& stream, const Type& type) {
    return stream << type.typestr();
}

namespace detail {

inline TypePtr require_type(TypePtr content, const char* owner) {
    if (!content) {
        throw std::invalid_argument(std::string(owner) + " content must not be null");
    }
    return content;
}

}  // namespace detail

class UnknownType final : public Type {
public:
    TypeKind kind() const noexcept override { return TypeKind::unknown; }
    std::string typestr() const override { return "unknown"; }
    std::size_t ndim() const noexcept override { return 0; }
};

class NumpyType final : public Type {
public:
    explicit NumpyType(std::string primitive) : primitive_(std::move(primitive)) {
        if (primitive_.empty()) {
            throw std::invalid_argument("ak::NumpyType primitive must not be empty");
        }
    }

    TypeKind kind() const noexcept override { return TypeKind::numpy; }
    std::string typestr() const override { return primitive_; }
    std::size_t ndim() const noexcept override { return 0; }
    const std::string& primitive() const noexcept { return primitive_; }

private:
    std::string primitive_;
};

class ListType final : public Type {
public:
    explicit ListType(TypePtr content) : content_(detail::require_type(std::move(content), "ak::ListType")) {}

    TypeKind kind() const noexcept override { return TypeKind::list; }
    std::string typestr() const override { return "var * " + content_->typestr(); }
    std::size_t ndim() const noexcept override { return content_->ndim() + 1; }
    const Type& content() const noexcept { return *content_; }

private:
    TypePtr content_;
};

class RegularType final : public Type {
public:
    RegularType(TypePtr content, std::size_t size)
        : content_(detail::require_type(std::move(content), "ak::RegularType")), size_(size) {}

    TypeKind kind() const noexcept override { return TypeKind::regular; }
    std::string typestr() const override { return std::to_string(size_) + " * " + content_->typestr(); }
    std::size_t ndim() const noexcept override { return content_->ndim() + 1; }
    const Type& content() const noexcept { return *content_; }
    std::size_t size() const noexcept { return size_; }

private:
    TypePtr content_;
    std::size_t size_;
};

class OptionType final : public Type {
public:
    explicit OptionType(TypePtr content) : content_(detail::require_type(std::move(content), "ak::OptionType")) {}

    TypeKind kind() const noexcept override { return TypeKind::option; }
    std::string typestr() const override { return "?" + content_->typestr(); }
    std::size_t ndim() const noexcept override { return content_->ndim(); }
    const Type& content() const noexcept { return *content_; }

private:
    TypePtr content_;
};

class RecordType final : public Type {
public:
    RecordType(std::vector<std::string> fields,
               std::vector<TypePtr> contents,
               bool is_tuple = false,
               std::string name = {})
        : fields_(std::move(fields)), contents_(std::move(contents)), is_tuple_(is_tuple), name_(std::move(name)) {
        if (fields_.size() != contents_.size()) {
            throw std::invalid_argument("ak::RecordType fields and contents must have matching sizes");
        }
        if (std::any_of(contents_.begin(), contents_.end(), [](const auto& content) { return !content; })) {
            throw std::invalid_argument("ak::RecordType contents must not be null");
        }
    }

    TypeKind kind() const noexcept override { return TypeKind::record; }

    std::string typestr() const override {
        std::string result = name_;
        result += is_tuple_ ? '(' : '{';
        for (std::size_t i = 0; i < contents_.size(); ++i) {
            if (i != 0) result += ", ";
            if (!is_tuple_) result += fields_[i] + ": ";
            result += contents_[i]->typestr();
        }
        if (is_tuple_ && contents_.size() == 1) result += ',';
        result += is_tuple_ ? ')' : '}';
        return result;
    }

    std::size_t ndim() const noexcept override {
        std::size_t result = 0;
        for (const auto& content : contents_) result = std::max(result, content->ndim());
        return result;
    }

    const std::vector<std::string>& fields() const noexcept { return fields_; }
    const std::vector<TypePtr>& contents() const noexcept { return contents_; }
    bool is_tuple() const noexcept { return is_tuple_; }
    const std::string& name() const noexcept { return name_; }

private:
    std::vector<std::string> fields_;
    std::vector<TypePtr> contents_;
    bool is_tuple_;
    std::string name_;
};

class UnionType final : public Type {
public:
    explicit UnionType(std::vector<TypePtr> contents) : contents_(std::move(contents)) {
        if (contents_.empty() ||
            std::any_of(contents_.begin(), contents_.end(), [](const auto& content) { return !content; })) {
            throw std::invalid_argument("ak::UnionType requires non-null contents");
        }
    }

    TypeKind kind() const noexcept override { return TypeKind::union_; }

    std::string typestr() const override {
        std::string result = "union[";
        for (std::size_t i = 0; i < contents_.size(); ++i) {
            if (i != 0) result += ", ";
            result += contents_[i]->typestr();
        }
        return result + ']';
    }

    std::size_t ndim() const noexcept override {
        std::size_t result = 0;
        for (const auto& content : contents_) result = std::max(result, content->ndim());
        return result;
    }

    const std::vector<TypePtr>& contents() const noexcept { return contents_; }

private:
    std::vector<TypePtr> contents_;
};

class ArrayType final : public Type {
public:
    ArrayType(TypePtr content, std::size_t length)
        : content_(detail::require_type(std::move(content), "ak::ArrayType")), length_(length) {}

    TypeKind kind() const noexcept override { return TypeKind::array; }
    std::string typestr() const override { return std::to_string(length_) + " * " + content_->typestr(); }
    std::size_t ndim() const noexcept override { return content_->ndim() + 1; }
    const Type& content() const noexcept { return *content_; }
    std::size_t length() const noexcept { return length_; }

private:
    TypePtr content_;
    std::size_t length_;
};

class ScalarType final : public Type {
public:
    explicit ScalarType(TypePtr content) : content_(detail::require_type(std::move(content), "ak::ScalarType")) {}

    TypeKind kind() const noexcept override { return TypeKind::scalar; }
    std::string typestr() const override { return content_->typestr(); }
    std::size_t ndim() const noexcept override { return content_->ndim(); }
    const Type& content() const noexcept { return *content_; }

private:
    TypePtr content_;
};

namespace detail {

inline TypePtr type_from_form(const Form& form) {
    const auto parameter = form.parameters.find("__array__");
    if (parameter != form.parameters.end() && parameter->second == "string") {
        return std::make_shared<NumpyType>("string");
    }

    switch (form.kind) {
    case FormKind::empty:
        return std::make_shared<UnknownType>();
    case FormKind::numpy:
        return std::make_shared<NumpyType>(form.primitive);
    case FormKind::list:
    case FormKind::list_offset:
        if (form.contents.size() != 1) throw std::invalid_argument("list form requires one content type");
        return std::make_shared<ListType>(type_from_form(form.contents.front()));
    case FormKind::regular:
        if (form.contents.size() != 1) throw std::invalid_argument("regular form requires one content type");
        return std::make_shared<RegularType>(type_from_form(form.contents.front()), form.size);
    case FormKind::indexed:
        if (form.contents.size() != 1) throw std::invalid_argument("indexed form requires one content type");
        return type_from_form(form.contents.front());
    case FormKind::indexed_option:
    case FormKind::byte_masked:
    case FormKind::bit_masked:
    case FormKind::unmasked:
        if (form.contents.size() != 1) throw std::invalid_argument("option form requires one content type");
        return std::make_shared<OptionType>(type_from_form(form.contents.front()));
    case FormKind::record: {
        std::vector<TypePtr> contents;
        for (const auto& content : form.contents) contents.push_back(type_from_form(content));
        return std::make_shared<RecordType>(form.fields, std::move(contents), form.is_tuple, form.record_name);
    }
    case FormKind::union_: {
        std::vector<TypePtr> contents;
        for (const auto& content : form.contents) contents.push_back(type_from_form(content));
        return std::make_shared<UnionType>(std::move(contents));
    }
    }
    throw std::invalid_argument("unsupported form kind for type construction");
}

inline TypePtr scalar_type_from_value(const Value& value) {
    return std::visit(
        [](const auto& item) -> TypePtr {
            using Item = std::decay_t<decltype(item)>;
            if constexpr (std::same_as<Item, std::monostate>) {
                return std::make_shared<OptionType>(std::make_shared<UnknownType>());
            } else if constexpr (std::same_as<Item, bool>) {
                return std::make_shared<NumpyType>("bool");
            } else if constexpr (std::same_as<Item, std::int64_t>) {
                return std::make_shared<NumpyType>("int64");
            } else if constexpr (std::same_as<Item, double>) {
                return std::make_shared<NumpyType>("float64");
            } else if constexpr (std::same_as<Item, std::string>) {
                return std::make_shared<NumpyType>("string");
            } else if constexpr (std::same_as<Item, Value::record_type>) {
                std::vector<TypePtr> contents;
                for (const auto& field : item.values) contents.push_back(scalar_type_from_value(field));
                return std::make_shared<RecordType>(item.fields, std::move(contents), item.is_tuple);
            } else {
                throw std::invalid_argument("ak::Scalar cannot contain a list value");
            }
        },
        value.storage());
}

}  // namespace detail

}  // namespace ak
