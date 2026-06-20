#pragma once

#include <cstddef>
#include <cstdint>
#include <charconv>
#include <bit>
#include <concepts>
#include <cstring>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace ak {

enum class FormKind {
    empty,
    numpy,
    list,
    list_offset,
    regular,
    indexed,
    indexed_option,
    byte_masked,
    bit_masked,
    unmasked,
    record,
    union_,
};

using Buffer = std::variant<std::vector<bool>,
                            std::vector<std::uint8_t>,
                            std::vector<std::int64_t>,
                            std::vector<std::uint64_t>,
                            std::vector<float>,
                            std::vector<double>,
                            std::vector<std::string>>;

using BufferMap = std::map<std::string, Buffer>;

struct Form {
    FormKind kind{FormKind::empty};
    std::string key;
    std::string primitive;
    std::vector<Form> contents;
    std::vector<std::string> fields;
    bool is_tuple{false};
    std::string record_name;
    std::size_t length{0};
    std::size_t size{0};
    bool valid_when{true};
    bool lsb_order{true};
    std::map<std::string, std::string> parameters;

    friend bool operator==(const Form&, const Form&) = default;
};

struct ToBuffersResult {
    Form form;
    std::size_t length{0};
    BufferMap buffers;
};

namespace detail {

class BufferBuilder {
public:
    std::string next_key() {
        return "node" + std::to_string(next_id_++);
    }

    void add(std::string key, Buffer buffer) {
        buffers_.emplace(std::move(key), std::move(buffer));
    }

    BufferMap release() && {
        return std::move(buffers_);
    }

private:
    std::size_t next_id_{0};
    BufferMap buffers_;
};

template <typename T>
Buffer primitive_buffer_from_vector(const std::vector<T>& values) {
    using Type = std::remove_cvref_t<T>;
    if constexpr (std::same_as<Type, bool>) {
        return values;
    } else if constexpr (std::is_integral_v<Type> && std::is_signed_v<Type>) {
        std::vector<std::int64_t> result;
        result.reserve(values.size());
        for (const auto value : values) {
            result.push_back(static_cast<std::int64_t>(value));
        }
        return result;
    } else if constexpr (std::is_integral_v<Type> && std::is_unsigned_v<Type>) {
        std::vector<std::uint64_t> result;
        result.reserve(values.size());
        for (const auto value : values) {
            result.push_back(static_cast<std::uint64_t>(value));
        }
        return result;
    } else if constexpr (std::same_as<Type, float>) {
        return values;
    } else if constexpr (std::is_floating_point_v<Type>) {
        std::vector<double> result;
        result.reserve(values.size());
        for (const auto value : values) {
            result.push_back(static_cast<double>(value));
        }
        return result;
    } else {
        return values;
    }
}

inline Buffer index_buffer_from_offsets(const std::vector<std::size_t>& values) {
    std::vector<std::int64_t> result;
    result.reserve(values.size());
    for (const auto value : values) {
        result.push_back(static_cast<std::int64_t>(value));
    }
    return result;
}

inline Buffer index_buffer_from_signed(const std::vector<std::ptrdiff_t>& values) {
    std::vector<std::int64_t> result;
    result.reserve(values.size());
    for (const auto value : values) {
        result.push_back(static_cast<std::int64_t>(value));
    }
    return result;
}

inline std::string form_kind_name(FormKind kind) {
    switch (kind) {
    case FormKind::empty: return "empty";
    case FormKind::numpy: return "numpy";
    case FormKind::list: return "list";
    case FormKind::list_offset: return "list_offset";
    case FormKind::regular: return "regular";
    case FormKind::indexed: return "indexed";
    case FormKind::indexed_option: return "indexed_option";
    case FormKind::byte_masked: return "byte_masked";
    case FormKind::bit_masked: return "bit_masked";
    case FormKind::unmasked: return "unmasked";
    case FormKind::record: return "record";
    case FormKind::union_: return "union";
    }
    throw std::invalid_argument("unknown form kind");
}

inline FormKind form_kind_from_name(std::string_view name) {
    if (name == "empty") return FormKind::empty;
    if (name == "numpy") return FormKind::numpy;
    if (name == "list") return FormKind::list;
    if (name == "list_offset") return FormKind::list_offset;
    if (name == "regular") return FormKind::regular;
    if (name == "indexed") return FormKind::indexed;
    if (name == "indexed_option") return FormKind::indexed_option;
    if (name == "byte_masked") return FormKind::byte_masked;
    if (name == "bit_masked") return FormKind::bit_masked;
    if (name == "unmasked") return FormKind::unmasked;
    if (name == "record") return FormKind::record;
    if (name == "union") return FormKind::union_;
    throw std::invalid_argument("unknown form kind: " + std::string(name));
}

inline void append_json_string(std::string& output, std::string_view value) {
    static constexpr char digits[] = "0123456789abcdef";
    output.push_back('"');
    for (const auto character : value) {
        const auto byte = static_cast<unsigned char>(character);
        switch (character) {
        case '"': output += "\\\""; break;
        case '\\': output += "\\\\"; break;
        case '\b': output += "\\b"; break;
        case '\f': output += "\\f"; break;
        case '\n': output += "\\n"; break;
        case '\r': output += "\\r"; break;
        case '\t': output += "\\t"; break;
        default:
            if (byte < 0x20U) {
                output += "\\u00";
                output.push_back(digits[(byte >> 4U) & 0x0fU]);
                output.push_back(digits[byte & 0x0fU]);
            } else {
                output.push_back(character);
            }
        }
    }
    output.push_back('"');
}

inline void append_form_json(std::string& output, const Form& form) {
    output += "{\"schema_version\":1,\"kind\":";
    append_json_string(output, form_kind_name(form.kind));
    output += ",\"key\":";
    append_json_string(output, form.key);
    output += ",\"primitive\":";
    append_json_string(output, form.primitive);
    output += ",\"contents\":[";
    for (std::size_t i = 0; i < form.contents.size(); ++i) {
        if (i != 0) output.push_back(',');
        append_form_json(output, form.contents[i]);
    }
    output += "],\"fields\":[";
    for (std::size_t i = 0; i < form.fields.size(); ++i) {
        if (i != 0) output.push_back(',');
        append_json_string(output, form.fields[i]);
    }
    output += "],\"is_tuple\":";
    output += form.is_tuple ? "true" : "false";
    output += ",\"record_name\":";
    append_json_string(output, form.record_name);
    output += ",\"length\":" + std::to_string(form.length);
    output += ",\"size\":" + std::to_string(form.size);
    output += ",\"valid_when\":";
    output += form.valid_when ? "true" : "false";
    output += ",\"lsb_order\":";
    output += form.lsb_order ? "true" : "false";
    output += ",\"parameters\":{";
    std::size_t parameter_index = 0;
    for (const auto& [name, value] : form.parameters) {
        if (parameter_index++ != 0) output.push_back(',');
        append_json_string(output, name);
        output.push_back(':');
        append_json_string(output, value);
    }
    output += "}}";
}

class FormJsonParser {
public:
    explicit FormJsonParser(std::string_view input) : input_(input) {}

    Form parse() {
        auto form = parse_form();
        skip_space();
        if (position_ != input_.size()) fail("unexpected trailing JSON content");
        return form;
    }

private:
    [[noreturn]] void fail(const char* message) const {
        throw std::invalid_argument(std::string("invalid form JSON at byte ") + std::to_string(position_) +
                                    ": " + message);
    }

    void skip_space() {
        while (position_ < input_.size() &&
               (input_[position_] == ' ' || input_[position_] == '\n' || input_[position_] == '\r' ||
                input_[position_] == '\t')) {
            ++position_;
        }
    }

    bool consume(char expected) {
        skip_space();
        if (position_ < input_.size() && input_[position_] == expected) {
            ++position_;
            return true;
        }
        return false;
    }

    void require(char expected, const char* message) {
        if (!consume(expected)) fail(message);
    }

    std::string parse_string() {
        require('"', "expected string");
        std::string result;
        while (position_ < input_.size()) {
            const auto character = input_[position_++];
            if (character == '"') return result;
            if (character != '\\') {
                if (static_cast<unsigned char>(character) < 0x20U) fail("unescaped control character");
                result.push_back(character);
                continue;
            }
            if (position_ >= input_.size()) fail("incomplete escape sequence");
            const auto escaped = input_[position_++];
            switch (escaped) {
            case '"': result.push_back('"'); break;
            case '\\': result.push_back('\\'); break;
            case '/': result.push_back('/'); break;
            case 'b': result.push_back('\b'); break;
            case 'f': result.push_back('\f'); break;
            case 'n': result.push_back('\n'); break;
            case 'r': result.push_back('\r'); break;
            case 't': result.push_back('\t'); break;
            case 'u': {
                if (position_ + 4 > input_.size() || input_.substr(position_, 2) != "00") {
                    fail("only byte-sized Unicode escapes are supported");
                }
                const auto hex = input_.substr(position_ + 2, 2);
                unsigned int value = 0;
                const auto parsed = std::from_chars(hex.data(), hex.data() + hex.size(), value, 16);
                if (parsed.ec != std::errc{} || parsed.ptr != hex.data() + hex.size()) fail("invalid Unicode escape");
                result.push_back(static_cast<char>(value));
                position_ += 4;
                break;
            }
            default: fail("invalid escape sequence");
            }
        }
        fail("unterminated string");
    }

    std::size_t parse_size() {
        skip_space();
        const auto begin = input_.data() + position_;
        const auto end = input_.data() + input_.size();
        std::size_t result = 0;
        const auto parsed = std::from_chars(begin, end, result);
        if (parsed.ec != std::errc{} || parsed.ptr == begin) fail("expected non-negative integer");
        position_ = static_cast<std::size_t>(parsed.ptr - input_.data());
        return result;
    }

    bool parse_bool() {
        skip_space();
        if (input_.substr(position_, 4) == "true") {
            position_ += 4;
            return true;
        }
        if (input_.substr(position_, 5) == "false") {
            position_ += 5;
            return false;
        }
        fail("expected boolean");
    }

    std::vector<std::string> parse_strings() {
        require('[', "expected string array");
        std::vector<std::string> result;
        if (consume(']')) return result;
        do {
            result.push_back(parse_string());
        } while (consume(','));
        require(']', "expected end of string array");
        return result;
    }

    std::vector<Form> parse_contents() {
        require('[', "expected contents array");
        std::vector<Form> result;
        if (consume(']')) return result;
        do {
            result.push_back(parse_form());
        } while (consume(','));
        require(']', "expected end of contents array");
        return result;
    }

    std::map<std::string, std::string> parse_parameters() {
        require('{', "expected parameters object");
        std::map<std::string, std::string> result;
        if (consume('}')) return result;
        do {
            auto name = parse_string();
            require(':', "expected parameter separator");
            auto [unused, inserted] = result.emplace(std::move(name), parse_string());
            (void)unused;
            if (!inserted) fail("duplicate parameter");
        } while (consume(','));
        require('}', "expected end of parameters object");
        return result;
    }

    Form parse_form() {
        require('{', "expected form object");
        Form form;
        bool has_kind = false;
        std::optional<std::size_t> schema_version;
        std::set<std::string> members;
        if (consume('}')) fail("form object must not be empty");
        do {
            const auto name = parse_string();
            if (!members.insert(name).second) fail("duplicate form object member");
            require(':', "expected object member separator");
            if (name == "schema_version") {
                schema_version = parse_size();
            } else if (name == "kind") {
                form.kind = form_kind_from_name(parse_string());
                has_kind = true;
            } else if (name == "key") {
                form.key = parse_string();
            } else if (name == "primitive") {
                form.primitive = parse_string();
            } else if (name == "contents") {
                form.contents = parse_contents();
            } else if (name == "fields") {
                form.fields = parse_strings();
            } else if (name == "is_tuple") {
                form.is_tuple = parse_bool();
            } else if (name == "record_name") {
                form.record_name = parse_string();
            } else if (name == "length") {
                form.length = parse_size();
            } else if (name == "size") {
                form.size = parse_size();
            } else if (name == "valid_when") {
                form.valid_when = parse_bool();
            } else if (name == "lsb_order") {
                form.lsb_order = parse_bool();
            } else if (name == "parameters") {
                form.parameters = parse_parameters();
            } else {
                fail("unknown form object member");
            }
        } while (consume(','));
        require('}', "expected end of form object");
        static const std::set<std::string> required{
            "schema_version", "kind", "key", "primitive", "contents", "fields", "is_tuple",
            "record_name", "length", "size", "valid_when", "lsb_order", "parameters"};
        if (members != required) fail("form object has missing required members");
        if (!has_kind) fail("form object is missing kind");
        if (!schema_version || *schema_version != 1) fail("unsupported form schema version");
        return form;
    }

    std::string_view input_;
    std::size_t position_{0};
};

class BufferBinaryWriter {
public:
    void byte(std::uint8_t value) { output_.push_back(value); }

    template <typename T>
    void unsigned_integer(T value) requires std::is_unsigned_v<T> {
        for (std::size_t i = 0; i < sizeof(T); ++i) byte(static_cast<std::uint8_t>(value >> (i * 8U)));
    }

    void bytes(std::span<const std::uint8_t> values) {
        output_.insert(output_.end(), values.begin(), values.end());
    }

    void string(std::string_view value) {
        unsigned_integer<std::uint64_t>(value.size());
        bytes(std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(value.data()), value.size()));
    }

    std::vector<std::uint8_t> release() && { return std::move(output_); }

private:
    std::vector<std::uint8_t> output_;
};

class BufferBinaryReader {
public:
    explicit BufferBinaryReader(std::span<const std::uint8_t> input) : input_(input) {}

    std::uint8_t byte() {
        require(1);
        return input_[position_++];
    }

    template <typename T>
    T unsigned_integer() requires std::is_unsigned_v<T> {
        require(sizeof(T));
        T result = 0;
        for (std::size_t i = 0; i < sizeof(T); ++i) result |= static_cast<T>(input_[position_++]) << (i * 8U);
        return result;
    }

    std::string string() {
        const auto size = unsigned_integer<std::uint64_t>();
        if (size > std::numeric_limits<std::size_t>::max()) fail("string length exceeds platform size");
        require(static_cast<std::size_t>(size));
        const auto* begin = reinterpret_cast<const char*>(input_.data() + position_);
        position_ += static_cast<std::size_t>(size);
        return std::string(begin, static_cast<std::size_t>(size));
    }

    bool done() const noexcept { return position_ == input_.size(); }

private:
    [[noreturn]] void fail(const char* message) const {
        throw std::invalid_argument(std::string("invalid buffer binary at byte ") + std::to_string(position_) +
                                    ": " + message);
    }

    void require(std::size_t count) const {
        if (count > input_.size() - position_) fail("truncated input");
    }

    std::span<const std::uint8_t> input_;
    std::size_t position_{0};
};

template <typename T>
void write_numeric_vector(BufferBinaryWriter& writer, const std::vector<T>& values) {
    writer.unsigned_integer<std::uint64_t>(values.size());
    for (const auto value : values) {
        if constexpr (std::same_as<T, std::uint8_t>) {
            writer.byte(value);
        } else if constexpr (std::same_as<T, std::int64_t>) {
            writer.unsigned_integer<std::uint64_t>(std::bit_cast<std::uint64_t>(value));
        } else if constexpr (std::same_as<T, std::uint64_t>) {
            writer.unsigned_integer<std::uint64_t>(value);
        } else if constexpr (std::same_as<T, float>) {
            writer.unsigned_integer<std::uint32_t>(std::bit_cast<std::uint32_t>(value));
        } else if constexpr (std::same_as<T, double>) {
            writer.unsigned_integer<std::uint64_t>(std::bit_cast<std::uint64_t>(value));
        }
    }
}

template <typename T>
std::vector<T> read_numeric_vector(BufferBinaryReader& reader, std::uint64_t count) {
    if (count > std::numeric_limits<std::size_t>::max()) {
        throw std::invalid_argument("buffer element count exceeds platform size");
    }
    std::vector<T> values;
    values.reserve(static_cast<std::size_t>(count));
    for (std::uint64_t i = 0; i < count; ++i) {
        if constexpr (std::same_as<T, std::uint8_t>) {
            values.push_back(reader.byte());
        } else if constexpr (std::same_as<T, std::int64_t>) {
            values.push_back(std::bit_cast<std::int64_t>(reader.unsigned_integer<std::uint64_t>()));
        } else if constexpr (std::same_as<T, std::uint64_t>) {
            values.push_back(reader.unsigned_integer<std::uint64_t>());
        } else if constexpr (std::same_as<T, float>) {
            values.push_back(std::bit_cast<float>(reader.unsigned_integer<std::uint32_t>()));
        } else if constexpr (std::same_as<T, double>) {
            values.push_back(std::bit_cast<double>(reader.unsigned_integer<std::uint64_t>()));
        }
    }
    return values;
}

inline void write_buffer(BufferBinaryWriter& writer, const Buffer& buffer) {
    writer.byte(static_cast<std::uint8_t>(buffer.index()));
    std::visit(
        [&writer](const auto& values) {
            using Values = std::decay_t<decltype(values)>;
            using T = typename Values::value_type;
            if constexpr (std::same_as<T, bool>) {
                writer.unsigned_integer<std::uint64_t>(values.size());
                for (const auto value : values) writer.byte(value ? 1U : 0U);
            } else if constexpr (std::same_as<T, std::string>) {
                writer.unsigned_integer<std::uint64_t>(values.size());
                for (const auto& value : values) writer.string(value);
            } else {
                write_numeric_vector(writer, values);
            }
        },
        buffer);
}

inline Buffer read_buffer(BufferBinaryReader& reader) {
    const auto tag = reader.byte();
    const auto count = reader.unsigned_integer<std::uint64_t>();
    switch (tag) {
    case 0: {
        if (count > std::numeric_limits<std::size_t>::max()) throw std::invalid_argument("bool buffer is too large");
        std::vector<bool> values;
        values.reserve(static_cast<std::size_t>(count));
        for (std::uint64_t i = 0; i < count; ++i) {
            const auto value = reader.byte();
            if (value > 1) throw std::invalid_argument("bool buffer contains an invalid byte");
            values.push_back(value != 0);
        }
        return values;
    }
    case 1: return read_numeric_vector<std::uint8_t>(reader, count);
    case 2: return read_numeric_vector<std::int64_t>(reader, count);
    case 3: return read_numeric_vector<std::uint64_t>(reader, count);
    case 4: return read_numeric_vector<float>(reader, count);
    case 5: return read_numeric_vector<double>(reader, count);
    case 6: {
        if (count > std::numeric_limits<std::size_t>::max()) throw std::invalid_argument("string buffer is too large");
        std::vector<std::string> values;
        values.reserve(static_cast<std::size_t>(count));
        for (std::uint64_t i = 0; i < count; ++i) values.push_back(reader.string());
        return values;
    }
    default: throw std::invalid_argument("buffer binary contains an invalid type tag");
    }
}

}  // namespace detail

inline std::string to_json(const Form& form) {
    std::string result;
    detail::append_form_json(result, form);
    return result;
}

inline Form form_from_json(std::string_view json) {
    return detail::FormJsonParser(json).parse();
}

inline std::vector<std::uint8_t> to_binary(const BufferMap& buffers) {
    detail::BufferBinaryWriter writer;
    writer.bytes(std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>("AKBF"), 4));
    writer.unsigned_integer<std::uint32_t>(1);
    writer.unsigned_integer<std::uint64_t>(buffers.size());
    for (const auto& [key, buffer] : buffers) {
        writer.string(key);
        detail::write_buffer(writer, buffer);
    }
    return std::move(writer).release();
}

inline BufferMap buffers_from_binary(std::span<const std::uint8_t> binary) {
    detail::BufferBinaryReader reader(binary);
    if (reader.byte() != 'A' || reader.byte() != 'K' || reader.byte() != 'B' || reader.byte() != 'F') {
        throw std::invalid_argument("buffer binary has an invalid magic value");
    }
    if (reader.unsigned_integer<std::uint32_t>() != 1) {
        throw std::invalid_argument("buffer binary has an unsupported schema version");
    }
    const auto count = reader.unsigned_integer<std::uint64_t>();
    if (count > std::numeric_limits<std::size_t>::max()) throw std::invalid_argument("buffer map is too large");
    BufferMap result;
    for (std::uint64_t i = 0; i < count; ++i) {
        auto key = reader.string();
        auto [unused, inserted] = result.emplace(std::move(key), detail::read_buffer(reader));
        (void)unused;
        if (!inserted) throw std::invalid_argument("buffer binary contains a duplicate key");
    }
    if (!reader.done()) throw std::invalid_argument("buffer binary contains trailing bytes");
    return result;
}

}  // namespace ak
