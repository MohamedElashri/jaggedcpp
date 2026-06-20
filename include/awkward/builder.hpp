#pragma once

#include "awkward/operations.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace ak {

class ArrayBuilder {
public:
    ArrayBuilder() = default;

    std::size_t length() const noexcept {
        return values_.size();
    }

    void null() {
        append_value(Value(nullptr));
    }

    void boolean(bool value) {
        append_value(Value(value));
    }

    void integer(std::int64_t value) {
        append_value(Value(value));
    }

    void real(double value) {
        append_value(Value(value));
    }

    void string(std::string_view value) {
        append_value(Value(std::string(value)));
    }

    void begin_list() {
        ensure_accepts_value();
        contexts_.emplace_back(ListContext{});
    }

    void end_list() {
        require_context<ListContext>("end_list requires an open list");
        auto context = std::move(std::get<ListContext>(contexts_.back()));
        contexts_.pop_back();
        append_value(Value(std::move(context.values)));
    }

    void begin_tuple(std::size_t num_fields) {
        ensure_accepts_value();
        contexts_.emplace_back(TupleContext{std::vector<std::optional<Value>>(num_fields), std::nullopt});
    }

    void index(std::size_t field_index) {
        auto& context = require_context<TupleContext>("index requires an open tuple");
        if (field_index >= context.values.size()) {
            throw std::out_of_range("tuple index is outside the declared field count");
        }
        if (context.selected) {
            throw std::invalid_argument("tuple index already selected for the next value");
        }
        if (context.values[field_index]) {
            throw std::invalid_argument("tuple field already has a value");
        }
        context.selected = field_index;
    }

    void end_tuple() {
        auto& current = require_context<TupleContext>("end_tuple requires an open tuple");
        if (current.selected) {
            throw std::invalid_argument("selected tuple field does not have a value");
        }
        for (const auto& value : current.values) {
            if (!value) {
                throw std::invalid_argument("all tuple fields must be assigned before end_tuple");
            }
        }

        auto context = std::move(std::get<TupleContext>(contexts_.back()));
        contexts_.pop_back();
        Value::record_type tuple;
        tuple.is_tuple = true;
        tuple.fields.reserve(context.values.size());
        tuple.values.reserve(context.values.size());
        for (std::size_t i = 0; i < context.values.size(); ++i) {
            tuple.fields.push_back(std::to_string(i));
            tuple.values.push_back(std::move(*context.values[i]));
        }
        append_value(Value(std::move(tuple)));
    }

    void begin_record() {
        ensure_accepts_value();
        contexts_.emplace_back(RecordContext{});
    }

    void field(std::string_view name) {
        auto& context = require_context<RecordContext>("field requires an open record");
        if (name.empty()) {
            throw std::invalid_argument("record field name must not be empty");
        }
        if (context.pending_field) {
            throw std::invalid_argument("record field already selected for the next value");
        }
        const std::string field_name(name);
        for (const auto& existing : context.fields) {
            if (existing == field_name) {
                throw std::invalid_argument("record field already has a value: " + field_name);
            }
        }
        context.pending_field = field_name;
    }

    void end_record() {
        auto& current = require_context<RecordContext>("end_record requires an open record");
        if (current.pending_field) {
            throw std::invalid_argument("selected record field does not have a value");
        }

        auto context = std::move(std::get<RecordContext>(contexts_.back()));
        contexts_.pop_back();
        Value::record_type record;
        record.fields = std::move(context.fields);
        record.values = std::move(context.values);
        append_value(Value(std::move(record)));
    }

    Array snapshot() const {
        if (!contexts_.empty()) {
            throw std::invalid_argument("snapshot requires all lists, tuples, and records to be closed");
        }
        if (values_.empty()) {
            return Array();
        }
        return detail::array_from_list(values_);
    }

private:
    struct ListContext {
        Value::list_type values;
    };

    struct TupleContext {
        std::vector<std::optional<Value>> values;
        std::optional<std::size_t> selected;
    };

    struct RecordContext {
        std::vector<std::string> fields;
        std::vector<Value> values;
        std::optional<std::string> pending_field;
    };

    using Context = std::variant<ListContext, TupleContext, RecordContext>;

    template <typename T>
    T& require_context(const char* message) {
        if (contexts_.empty() || !std::holds_alternative<T>(contexts_.back())) {
            throw std::invalid_argument(message);
        }
        return std::get<T>(contexts_.back());
    }

    void ensure_accepts_value() const {
        if (contexts_.empty() || std::holds_alternative<ListContext>(contexts_.back())) {
            return;
        }
        if (const auto* tuple = std::get_if<TupleContext>(&contexts_.back())) {
            if (!tuple->selected) {
                throw std::invalid_argument("tuple index must be selected before adding a value");
            }
            return;
        }
        if (!std::get<RecordContext>(contexts_.back()).pending_field) {
            throw std::invalid_argument("record field must be selected before adding a value");
        }
    }

    void append_value(Value value) {
        ensure_accepts_value();
        if (contexts_.empty()) {
            values_.push_back(std::move(value));
            return;
        }
        if (auto* list = std::get_if<ListContext>(&contexts_.back())) {
            list->values.push_back(std::move(value));
            return;
        }
        if (auto* tuple = std::get_if<TupleContext>(&contexts_.back())) {
            tuple->values[*tuple->selected] = std::move(value);
            tuple->selected.reset();
            return;
        }
        auto& record = std::get<RecordContext>(contexts_.back());
        record.fields.push_back(std::move(*record.pending_field));
        record.values.push_back(std::move(value));
        record.pending_field.reset();
    }

    Value::list_type values_;
    std::vector<Context> contexts_;
};

}  // namespace ak
