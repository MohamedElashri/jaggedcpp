#pragma once
// Amalgamated header for jaggedcpp

#include <algorithm>
#include <bit>
#include <charconv>
#include <cmath>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <ostream>
#include <set>
#include <span>
#include <sstream>
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

namespace ak {

class Array;
class Content;

namespace index {

struct Integer {
    std::ptrdiff_t value;
};

struct Slice {
    std::optional<std::ptrdiff_t> start;
    std::optional<std::ptrdiff_t> stop;
    std::ptrdiff_t step{1};
};

struct Ellipsis {};
struct NewAxis {};

struct Field {
    std::string name;
};

struct Fields {
    std::vector<std::string> names;
};

struct IntegerArray {
    std::vector<std::ptrdiff_t> values;
};

struct BooleanArray {
    std::vector<bool> values;
};

struct ArrayIndex {
    std::shared_ptr<const Content> layout;
};

using Item = std::variant<Integer, Slice, Ellipsis, NewAxis, Field, Fields, IntegerArray, BooleanArray, ArrayIndex>;

inline Item at(std::ptrdiff_t value) {
    return Integer{value};
}

inline Item range(std::optional<std::ptrdiff_t> start,
                  std::optional<std::ptrdiff_t> stop,
                  std::ptrdiff_t step = 1) {
    return Slice{start, stop, step};
}

inline Item range(std::ptrdiff_t start, std::ptrdiff_t stop, std::ptrdiff_t step = 1) {
    return range(std::optional<std::ptrdiff_t>{start}, std::optional<std::ptrdiff_t>{stop}, step);
}

inline Item all() {
    return Slice{std::nullopt, std::nullopt, 1};
}

inline Item ellipsis() {
    return Ellipsis{};
}

inline Item newaxis() {
    return NewAxis{};
}

inline Item field(std::string name) {
    return Field{std::move(name)};
}

inline Item fields(std::vector<std::string> names) {
    return Fields{std::move(names)};
}

inline Item integers(std::vector<std::ptrdiff_t> values) {
    return IntegerArray{std::move(values)};
}

inline Item booleans(std::vector<bool> values) {
    return BooleanArray{std::move(values)};
}

namespace detail {

inline std::size_t normalize_integer(std::ptrdiff_t index, std::size_t length, const char* what) {
    const auto signed_length = static_cast<std::ptrdiff_t>(length);
    const auto normalized = index < 0 ? signed_length + index : index;
    if (normalized < 0 || normalized >= signed_length) {
        throw std::out_of_range(std::string(what) + " index is out of range");
    }
    return static_cast<std::size_t>(normalized);
}

inline std::vector<std::size_t> indices_for_slice(const Slice& slice, std::size_t length) {
    if (slice.step == 0) {
        throw std::invalid_argument("slice step must not be zero");
    }

    std::vector<std::size_t> result;
    const auto n = static_cast<std::ptrdiff_t>(length);

    if (slice.step > 0) {
        auto start = slice.start.value_or(0);
        auto stop = slice.stop.value_or(n);
        if (start < 0) {
            start += n;
        }
        if (stop < 0) {
            stop += n;
        }
        start = std::max<std::ptrdiff_t>(0, std::min(start, n));
        stop = std::max<std::ptrdiff_t>(0, std::min(stop, n));
        for (auto i = start; i < stop; i += slice.step) {
            result.push_back(static_cast<std::size_t>(i));
        }
    } else {
        auto start = slice.start.value_or(n - 1);
        auto stop = slice.stop.value_or(-1);
        if (start < 0) {
            start += n;
        }
        if (slice.stop && stop < 0) {
            stop += n;
        }
        start = std::max<std::ptrdiff_t>(-1, std::min(start, n - 1));
        stop = std::max<std::ptrdiff_t>(-1, std::min(stop, n - 1));
        for (auto i = start; i > stop; i += slice.step) {
            result.push_back(static_cast<std::size_t>(i));
        }
    }

    return result;
}

inline std::vector<std::size_t> normalize_integer_array(const std::vector<std::ptrdiff_t>& values,
                                                        std::size_t length,
                                                        const char* what) {
    std::vector<std::size_t> result;
    result.reserve(values.size());
    for (const auto value : values) {
        result.push_back(normalize_integer(value, length, what));
    }
    return result;
}

}  // namespace detail

}  // namespace index

}  // namespace ak

namespace ak {

struct None {
    explicit constexpr None() = default;
};

inline constexpr None none{};

template <typename T>
class Option {
public:
    Option(None) noexcept : has_value_(false), value_{} {}
    Option(std::nullopt_t) noexcept : has_value_(false), value_{} {}
    Option(T value) : has_value_(true), value_(std::move(value)) {}

    bool has_value() const noexcept {
        return has_value_;
    }

    const T& value() const {
        if (!has_value_) {
            throw std::out_of_range("ak::Option does not contain a value");
        }
        return value_;
    }

private:
    bool has_value_;
    T value_;
};

class Value {
public:
    using list_type = std::vector<Value>;
    struct record_type {
        bool is_tuple{false};
        std::vector<std::string> fields;
        std::vector<Value> values;

        friend bool operator==(const record_type&, const record_type&) = default;
    };

    Value() = default;
    Value(std::nullptr_t) : value_(std::monostate{}) {}
    Value(bool value) : value_(value) {}
    Value(float value) : value_(static_cast<double>(value)) {}
    Value(double value) : value_(value) {}
    Value(const char* value) : value_(std::string(value)) {}
    Value(std::string value) : value_(std::move(value)) {}
    Value(list_type value) : value_(std::move(value)) {}
    Value(record_type value) : value_(std::move(value)) {}

    template <typename T>
    Value(T value) requires (std::is_integral_v<T> && !std::same_as<std::remove_cv_t<T>, bool>)
        : value_(static_cast<std::int64_t>(value)) {}

    const auto& storage() const noexcept {
        return value_;
    }

    bool is_none() const noexcept {
        return std::holds_alternative<std::monostate>(value_);
    }

    template <typename T>
    bool is() const noexcept {
        return std::holds_alternative<T>(value_);
    }

    template <typename T>
    const T& get() const {
        if (!is<T>()) {
            throw std::invalid_argument("ak::Value does not contain the requested C++ type");
        }
        return std::get<T>(value_);
    }

    const list_type& as_list() const {
        return std::get<list_type>(value_);
    }

    const record_type& as_record() const {
        return std::get<record_type>(value_);
    }

    friend bool operator==(const Value&, const Value&) = default;

private:
    std::variant<std::monostate, bool, std::int64_t, double, std::string, list_type, record_type> value_;
};

namespace detail {

inline void append_escaped(std::ostream& stream, const std::string& value) {
    stream << '"';
    for (const auto character : value) {
        if (character == '"' || character == '\\') {
            stream << '\\';
        }
        stream << character;
    }
    stream << '"';
}

inline void print_value(std::ostream& stream, const Value& value) {
    std::visit(
        [&stream](const auto& item) {
            using Item = std::decay_t<decltype(item)>;
            if constexpr (std::same_as<Item, std::monostate>) {
                stream << "None";
            } else if constexpr (std::same_as<Item, bool>) {
                stream << (item ? "true" : "false");
            } else if constexpr (std::same_as<Item, std::string>) {
                append_escaped(stream, item);
            } else if constexpr (std::same_as<Item, Value::list_type>) {
                stream << '[';
                for (std::size_t i = 0; i < item.size(); ++i) {
                    if (i != 0) {
                        stream << ", ";
                    }
                    print_value(stream, item[i]);
                }
                stream << ']';
            } else if constexpr (std::same_as<Item, Value::record_type>) {
                stream << (item.is_tuple ? '(' : '{');
                for (std::size_t i = 0; i < item.values.size(); ++i) {
                    if (i != 0) {
                        stream << ", ";
                    }
                    if (!item.is_tuple) {
                        stream << item.fields[i] << ": ";
                    }
                    print_value(stream, item.values[i]);
                }
                if (item.is_tuple && item.values.size() == 1) {
                    stream << ',';
                }
                stream << (item.is_tuple ? ')' : '}');
            } else {
                stream << item;
            }
        },
        value.storage());
}

}  // namespace detail

inline std::ostream& operator<<(std::ostream& stream, const Value& value) {
    detail::print_value(stream, value);
    return stream;
}

inline std::string to_string(const Value& value) {
    std::ostringstream stream;
    stream << value;
    return stream.str();
}

}  // namespace ak

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

namespace ak::detail {

template <typename T>
struct is_string_like
    : std::bool_constant<std::is_convertible_v<T, std::string_view> &&
                         !std::same_as<std::remove_cvref_t<T>, std::string_view>> {};

template <>
struct is_string_like<std::string> : std::true_type {};

template <typename T>
inline constexpr bool is_string_like_v = is_string_like<T>::value;

template <typename T>
using storage_type_t = std::conditional_t<is_string_like_v<T> && !std::same_as<std::remove_cvref_t<T>, std::string>,
                                          std::string,
                                          std::remove_cvref_t<T>>;

template <typename T>
storage_type_t<T> normalize_value(const T& value) {
    if constexpr (is_string_like_v<T> && !std::same_as<std::remove_cvref_t<T>, std::string>) {
        return std::string(value);
    } else {
        return value;
    }
}

template <typename T>
Value scalar_to_value(const T& value) {
    if constexpr (is_string_like_v<T>) {
        return Value(std::string(value));
    } else {
        return Value(value);
    }
}

template <typename T>
std::string primitive_type_name() {
    using Type = std::remove_cvref_t<T>;
    if constexpr (std::same_as<Type, bool>) {
        return "bool";
    } else if constexpr (std::is_integral_v<Type> && std::is_signed_v<Type>) {
        return "int64";
    } else if constexpr (std::is_integral_v<Type> && std::is_unsigned_v<Type>) {
        return "uint64";
    } else if constexpr (std::same_as<Type, float>) {
        return "float32";
    } else if constexpr (std::same_as<Type, double>) {
        return "float64";
    } else if constexpr (is_string_like_v<Type>) {
        return "string";
    } else {
        return "unknown";
    }
}

template <typename T>
std::size_t primitive_nbytes(const std::vector<T>& values) {
    using Type = std::remove_cvref_t<T>;
    if constexpr (std::same_as<Type, bool>) {
        return values.size() * sizeof(bool);
    } else if constexpr (std::is_integral_v<Type>) {
        return values.size() * sizeof(std::int64_t);
    } else if constexpr (std::same_as<Type, float>) {
        return values.size() * sizeof(float);
    } else if constexpr (std::same_as<Type, double>) {
        return values.size() * sizeof(double);
    } else if constexpr (is_string_like_v<Type>) {
        std::size_t total = 0;
        for (const auto& value : values) {
            total += value.size();
        }
        return total + (values.size() + 1) * sizeof(std::int64_t);
    } else {
        return values.size() * sizeof(Type);
    }
}

inline std::string list_type_string(std::size_t length, const std::string& item_type) {
    return std::to_string(length) + " * var * " + item_type;
}

inline std::string item_type_from_typestr(const std::string& typestr) {
    const auto separator = typestr.find(" * ");
    if (separator == std::string::npos) {
        return typestr;
    }
    return typestr.substr(separator + 3);
}

template <typename T>
Value vector_to_value(const std::vector<T>& values) {
    Value::list_type list;
    list.reserve(values.size());
    for (const auto& value : values) {
        list.emplace_back(scalar_to_value(value));
    }
    return Value(std::move(list));
}

template <typename T>
Value span_to_value(std::span<const T> values) {
    Value::list_type list;
    list.reserve(values.size());
    for (const auto& value : values) {
        list.emplace_back(scalar_to_value(value));
    }
    return Value(std::move(list));
}

}  // namespace ak::detail

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

namespace ak {

class IndexedArray final : public Content {
public:
    IndexedArray(std::vector<std::ptrdiff_t> index, std::shared_ptr<const Content> content)
        : index_(std::move(index)), content_(std::move(content)) {
        const auto error = validate();
        if (!error.empty()) throw std::invalid_argument(error);
    }

    LayoutKind kind() const noexcept override { return LayoutKind::indexed; }
    std::size_t length() const noexcept override { return index_.size(); }
    std::size_t nbytes() const noexcept override { return index_.size() * sizeof(std::ptrdiff_t) + content_->nbytes(); }

    std::string typestr() const override {
        return std::to_string(length()) + " * " + detail::item_type_from_typestr(content_->typestr());
    }

    std::string validity_error() const override { return validate(); }

    Value to_list() const override {
        Value::list_type result;
        result.reserve(length());
        for (const auto item : index_) result.push_back(content_->at(item));
        return result;
    }

    Value at(std::ptrdiff_t index) const override {
        return content_->at(index_[index::detail::normalize_integer(index, length(), "array")]);
    }

    Value at(std::ptrdiff_t outer, std::ptrdiff_t inner) const override {
        return content_->at(index_[index::detail::normalize_integer(outer, length(), "row")], inner);
    }

    std::shared_ptr<const Content> slice_rows(const index::Slice& slice) const override {
        return select(index::detail::indices_for_slice(slice, length()));
    }

    std::shared_ptr<const Content> take_rows(const std::vector<std::ptrdiff_t>& indices) const override {
        return select(index::detail::normalize_integer_array(indices, length(), "array"));
    }

    std::shared_ptr<const Content> mask_rows(const std::vector<bool>& mask) const override {
        if (mask.size() != length()) throw std::invalid_argument("boolean mask length must match array length");
        std::vector<std::size_t> selected;
        for (std::size_t i = 0; i < mask.size(); ++i) if (mask[i]) selected.push_back(i);
        return select(selected);
    }

    std::shared_ptr<const Content> mask_as_array(const Content& mask) const override {
        const auto* values = mask.flat_bool_mask();
        if (!values) throw std::invalid_argument("indexed array requires a flat boolean mask");
        return mask_rows(*values);
    }

    std::vector<std::string> fields() const override { return content_->fields(); }
    bool is_tuple() const noexcept override { return content_->is_tuple(); }
    std::string record_name() const override { return content_->record_name(); }

    std::shared_ptr<const Content> field(const std::string& name) const override {
        return std::make_shared<IndexedArray>(index_, content_->field(name));
    }

    std::shared_ptr<const Content> project_fields(const std::vector<std::string>& names) const override {
        return std::make_shared<IndexedArray>(index_, content_->project_fields(names));
    }

    std::shared_ptr<const Content> with_name(std::string name) const override {
        return std::make_shared<IndexedArray>(index_, content_->with_name(std::move(name)));
    }

    std::vector<std::size_t> num() const override { return to_packed()->num(); }

    std::shared_ptr<const Content> slice_inner(const index::Slice& rows,
                                               const index::Slice& columns) const override {
        return to_packed()->slice_inner(rows, columns);
    }

    std::shared_ptr<const Content> slice_one_inner(std::ptrdiff_t row,
                                                   const index::Slice& columns) const override {
        return to_packed()->slice_one_inner(row, columns);
    }

    std::shared_ptr<const Content> take_inner(const std::vector<std::ptrdiff_t>& rows,
                                              std::ptrdiff_t column) const override {
        return to_packed()->take_inner(rows, column);
    }

    std::shared_ptr<const Content> take_inner_array(const std::vector<std::ptrdiff_t>& rows,
                                                    const std::vector<std::ptrdiff_t>& columns) const override {
        return to_packed()->take_inner_array(rows, columns);
    }

    std::shared_ptr<const Content> take_one_inner_array(
        std::ptrdiff_t row, const std::vector<std::ptrdiff_t>& columns) const override {
        return to_packed()->take_one_inner_array(row, columns);
    }

    std::shared_ptr<const Content> flatten() const override {
        return to_packed()->flatten();
    }

    std::shared_ptr<const Content> to_packed() const override {
        return content_->take_rows(index_)->to_packed();
    }

    std::shared_ptr<const Content> local_index() const override {
        std::vector<std::int64_t> values;
        values.reserve(length());
        for (std::size_t i = 0; i < length(); ++i) values.push_back(static_cast<std::int64_t>(i));
        return std::make_shared<NumpyArray<std::int64_t>>(std::move(values));
    }

    const std::vector<std::ptrdiff_t>& index() const noexcept { return index_; }
    const Content& content() const noexcept { return *content_; }

    Form to_buffers(detail::BufferBuilder& builder) const override {
        auto key = builder.next_key();
        builder.add(key + "-index", detail::index_buffer_from_signed(index_));
        auto content_form = content_->to_buffers(builder);
        return Form{
            .kind = FormKind::indexed,
            .key = std::move(key),
            .contents = {std::move(content_form)},
            .length = length(),
        };
    }

private:
    std::shared_ptr<const Content> select(const std::vector<std::size_t>& positions) const {
        std::vector<std::ptrdiff_t> index;
        index.reserve(positions.size());
        for (const auto position : positions) index.push_back(index_[position]);
        return std::make_shared<IndexedArray>(std::move(index), content_);
    }

    std::string validate() const {
        if (!content_) return "ak::IndexedArray content must not be null";
        const auto error = content_->validity_error();
        if (!error.empty()) return error;
        for (const auto item : index_) {
            if (item < 0 || static_cast<std::size_t>(item) >= content_->length()) {
                return "ak::IndexedArray index entries must refer to content";
            }
        }
        return {};
    }

    std::vector<std::ptrdiff_t> index_;
    std::shared_ptr<const Content> content_;
};

}  // namespace ak

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

namespace ak {

class ListOffsetContentArray final : public Content {
public:
    ListOffsetContentArray(std::shared_ptr<const Content> content, std::vector<std::size_t> offsets)
        : content_(std::move(content)), offsets_(std::move(offsets)) {
        const auto error = validate();
        if (!error.empty()) {
            throw std::invalid_argument(error);
        }
    }

    LayoutKind kind() const noexcept override {
        return LayoutKind::list_offset_content;
    }

    std::size_t length() const noexcept override {
        return offsets_.size() - 1;
    }

    std::size_t nbytes() const noexcept override {
        return content_->nbytes() + offsets_.size() * sizeof(std::size_t);
    }

    std::string typestr() const override {
        return detail::list_type_string(length(), detail::item_type_from_typestr(content_->typestr()));
    }

    std::string validity_error() const override {
        return validate();
    }

    Value to_list() const override {
        Value::list_type rows;
        rows.reserve(length());
        for (std::size_t row = 0; row < length(); ++row) {
            Value::list_type values;
            values.reserve(offsets_[row + 1] - offsets_[row]);
            for (auto i = offsets_[row]; i < offsets_[row + 1]; ++i) {
                values.emplace_back(content_->at(static_cast<std::ptrdiff_t>(i)));
            }
            rows.emplace_back(std::move(values));
        }
        return rows;
    }

    Value at(std::ptrdiff_t index) const override {
        const auto row = index::detail::normalize_integer(index, length(), "row");
        Value::list_type values;
        values.reserve(offsets_[row + 1] - offsets_[row]);
        for (auto i = offsets_[row]; i < offsets_[row + 1]; ++i) {
            values.emplace_back(content_->at(static_cast<std::ptrdiff_t>(i)));
        }
        return values;
    }

    Value at(std::ptrdiff_t outer, std::ptrdiff_t inner) const override {
        const auto row = index::detail::normalize_integer(outer, length(), "row");
        const auto start = offsets_[row];
        const auto stop = offsets_[row + 1];
        const auto column = index::detail::normalize_integer(inner, stop - start, "column");
        return content_->at(static_cast<std::ptrdiff_t>(start + column));
    }

    std::shared_ptr<const Content> slice_rows(const index::Slice& slice) const override {
        return select_rows(index::detail::indices_for_slice(slice, length()));
    }

    std::shared_ptr<const Content> take_rows(const std::vector<std::ptrdiff_t>& indices) const override {
        return select_rows(index::detail::normalize_integer_array(indices, length(), "row"));
    }

    std::shared_ptr<const Content> mask_rows(const std::vector<bool>& mask) const override {
        if (mask.size() != length()) {
            throw std::invalid_argument("boolean mask length must match row count");
        }
        std::vector<std::size_t> rows;
        for (std::size_t i = 0; i < mask.size(); ++i) {
            if (mask[i]) {
                rows.push_back(i);
            }
        }
        return select_rows(rows);
    }

    std::shared_ptr<const Content> slice_inner(const index::Slice& rows,
                                               const index::Slice& columns) const override {
        return select_inner_slice(index::detail::indices_for_slice(rows, length()), columns, false);
    }

    std::shared_ptr<const Content> slice_one_inner(std::ptrdiff_t row,
                                                   const index::Slice& columns) const override {
        return select_inner_slice({index::detail::normalize_integer(row, length(), "row")}, columns, true);
    }

    std::shared_ptr<const Content> take_inner(const std::vector<std::ptrdiff_t>& rows,
                                              std::ptrdiff_t column) const override {
        const std::vector<std::ptrdiff_t> columns{column};
        auto selected = select_inner_array(index::detail::normalize_integer_array(rows, length(), "row"), columns, false);
        return selected->flatten();
    }

    std::shared_ptr<const Content> take_inner_array(const std::vector<std::ptrdiff_t>& rows,
                                                    const std::vector<std::ptrdiff_t>& columns) const override {
        return select_inner_array(index::detail::normalize_integer_array(rows, length(), "row"), columns, false);
    }

    std::shared_ptr<const Content> take_one_inner_array(
        std::ptrdiff_t row, const std::vector<std::ptrdiff_t>& columns) const override {
        return select_inner_array({index::detail::normalize_integer(row, length(), "row")}, columns, true);
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
        return std::make_shared<ListOffsetContentArray>(content_->field(name), offsets_);
    }

    std::shared_ptr<const Content> project_fields(const std::vector<std::string>& names) const override {
        return std::make_shared<ListOffsetContentArray>(content_->project_fields(names), offsets_);
    }

    std::shared_ptr<const Content> with_name(std::string name) const override {
        return std::make_shared<ListOffsetContentArray>(content_->with_name(std::move(name)), offsets_);
    }

    std::vector<std::size_t> num() const override {
        std::vector<std::size_t> result;
        result.reserve(length());
        for (std::size_t row = 0; row < length(); ++row) {
            result.push_back(offsets_[row + 1] - offsets_[row]);
        }
        return result;
    }

    std::shared_ptr<const Content> flatten() const override {
        return content_;
    }

    std::shared_ptr<const Content> to_packed() const override {
        return std::make_shared<ListOffsetContentArray>(content_->to_packed(), offsets_);
    }

    std::shared_ptr<const Content> local_index() const override {
        std::vector<std::int64_t> values;
        std::vector<std::size_t> offsets;
        offsets.reserve(length() + 1);
        offsets.push_back(0);
        for (std::size_t row = 0; row < length(); ++row) {
            for (std::size_t i = 0; i < offsets_[row + 1] - offsets_[row]; ++i) {
                values.push_back(static_cast<std::int64_t>(i));
            }
            offsets.push_back(values.size());
        }
        return std::make_shared<ListOffsetContentArray>(
            std::make_shared<NumpyArray<std::int64_t>>(std::move(values)), std::move(offsets));
    }

    Form to_buffers(detail::BufferBuilder& builder) const override {
        auto key = builder.next_key();
        builder.add(key + "-offsets", detail::index_buffer_from_offsets(offsets_));
        auto content_form = content_->to_buffers(builder);
        return Form{
            .kind = FormKind::list_offset,
            .key = std::move(key),
            .contents = {std::move(content_form)},
            .length = length(),
        };
    }

private:
    std::shared_ptr<const Content> select_inner_slice(const std::vector<std::size_t>& rows,
                                                      const index::Slice& columns,
                                                      bool drop_outer) const {
        std::vector<std::ptrdiff_t> selected;
        std::vector<std::size_t> offsets;
        offsets.reserve(rows.size() + 1);
        offsets.push_back(0);
        for (const auto row : rows) {
            const auto start = offsets_[row];
            const auto stop = offsets_[row + 1];
            for (const auto column : index::detail::indices_for_slice(columns, stop - start)) {
                selected.push_back(static_cast<std::ptrdiff_t>(start + column));
            }
            offsets.push_back(selected.size());
        }
        auto content = std::make_shared<IndexedArray>(std::move(selected), content_);
        if (drop_outer) return content;
        return std::make_shared<ListOffsetContentArray>(std::move(content), std::move(offsets));
    }

    std::shared_ptr<const Content> select_inner_array(const std::vector<std::size_t>& rows,
                                                      const std::vector<std::ptrdiff_t>& columns,
                                                      bool drop_outer) const {
        std::vector<std::ptrdiff_t> selected;
        std::vector<std::size_t> offsets;
        offsets.reserve(rows.size() + 1);
        offsets.push_back(0);
        for (const auto row : rows) {
            const auto start = offsets_[row];
            const auto stop = offsets_[row + 1];
            for (const auto column : index::detail::normalize_integer_array(columns, stop - start, "column")) {
                selected.push_back(static_cast<std::ptrdiff_t>(start + column));
            }
            offsets.push_back(selected.size());
        }
        auto content = std::make_shared<IndexedArray>(std::move(selected), content_);
        if (drop_outer) return content;
        return std::make_shared<ListOffsetContentArray>(std::move(content), std::move(offsets));
    }

    std::shared_ptr<const Content> select_rows(const std::vector<std::size_t>& rows) const {
        std::vector<std::ptrdiff_t> index;
        std::vector<std::size_t> offsets;
        offsets.reserve(rows.size() + 1);
        offsets.push_back(0);
        for (const auto row : rows) {
            for (auto i = offsets_[row]; i < offsets_[row + 1]; ++i) {
                index.push_back(static_cast<std::ptrdiff_t>(i));
            }
            offsets.push_back(index.size());
        }
        return std::make_shared<ListOffsetContentArray>(
            std::make_shared<IndexedArray>(std::move(index), content_), std::move(offsets));
    }

    std::string validate() const {
        if (!content_) {
            return "ak::ListOffsetContentArray content must not be null";
        }
        const auto content_error = content_->validity_error();
        if (!content_error.empty()) {
            return content_error;
        }
        if (offsets_.empty()) {
            return "ak::ListOffsetContentArray offsets must contain at least the initial zero offset";
        }
        if (offsets_.front() != 0) {
            return "ak::ListOffsetContentArray offsets must start at zero";
        }
        if (!std::is_sorted(offsets_.begin(), offsets_.end())) {
            return "ak::ListOffsetContentArray offsets must be monotonic";
        }
        if (offsets_.back() != content_->length()) {
            return "ak::ListOffsetContentArray final offset must equal content length";
        }
        return {};
    }

    std::shared_ptr<const Content> content_;
    std::vector<std::size_t> offsets_;
};

}  // namespace ak

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

namespace ak {

namespace detail {

std::shared_ptr<const Content> layout_from_list(const Value::list_type& values);

inline std::size_t layout_ndim(const Content& layout) {
    BufferBuilder builder;
    return type_from_form(layout.to_buffers(builder))->ndim() + 1;
}

inline std::vector<index::Item> normalize_slice_items(const Content& layout,
                                                      const std::vector<index::Item>& items) {
    std::vector<index::Item> normalized;
    std::size_t ellipsis_count = 0;
    std::size_t consuming_items = 0;
    for (const auto& item : items) {
        if (std::holds_alternative<index::Ellipsis>(item)) {
            ++ellipsis_count;
        } else if (!std::holds_alternative<index::NewAxis>(item) &&
                   !std::holds_alternative<index::Field>(item) &&
                   !std::holds_alternative<index::Fields>(item)) {
            ++consuming_items;
        }
    }
    if (ellipsis_count > 1) {
        throw std::invalid_argument("a slice may contain at most one ellipsis");
    }
    if (ellipsis_count == 0) return items;

    const auto ndim = layout_ndim(layout);
    if (consuming_items > ndim) {
        throw std::invalid_argument("slice has more dimension indexes than the array");
    }
    normalized.reserve(items.size() + ndim - consuming_items);
    for (const auto& item : items) {
        if (std::holds_alternative<index::Ellipsis>(item)) {
            for (std::size_t i = consuming_items; i < ndim; ++i) normalized.push_back(index::all());
        } else {
            normalized.push_back(item);
        }
    }
    return normalized;
}

inline std::shared_ptr<const Content> add_outer_axis(const Content& layout) {
    return std::make_shared<ListOffsetContentArray>(
        layout.to_packed(), std::vector<std::size_t>{0, layout.length()});
}

inline std::shared_ptr<const Content> add_singleton_axis(std::shared_ptr<const Content> layout) {
    std::vector<std::size_t> offsets;
    offsets.reserve(layout->length() + 1);
    for (std::size_t i = 0; i <= layout->length(); ++i) offsets.push_back(i);
    return std::make_shared<ListOffsetContentArray>(std::move(layout), std::move(offsets));
}

inline Value project_value_field(const Value& value, const std::vector<std::string>& names, bool single) {
    if (value.is_none()) return Value(nullptr);
    if (const auto* values = std::get_if<Value::list_type>(&value.storage())) {
        Value::list_type result;
        result.reserve(values->size());
        for (const auto& item : *values) result.push_back(project_value_field(item, names, single));
        return result;
    }
    const auto* record = std::get_if<Value::record_type>(&value.storage());
    if (!record) throw std::invalid_argument("field indexing requires record values");
    if (single) {
        const auto found = std::find(record->fields.begin(), record->fields.end(), names.front());
        if (found == record->fields.end()) throw std::out_of_range("record field does not exist: " + names.front());
        return record->values[static_cast<std::size_t>(std::distance(record->fields.begin(), found))];
    }
    Value::record_type result;
    result.is_tuple = record->is_tuple;
    result.fields = names;
    for (const auto& name : names) {
        const auto found = std::find(record->fields.begin(), record->fields.end(), name);
        if (found == record->fields.end()) throw std::out_of_range("record field does not exist: " + name);
        result.values.push_back(record->values[static_cast<std::size_t>(std::distance(record->fields.begin(), found))]);
    }
    return result;
}

inline Value slice_value_recursive(const Value& value,
                                   const std::vector<index::Item>& items,
                                   std::size_t position) {
    if (position == items.size()) return value;
    const auto& item = items[position];
    if (const auto* field = std::get_if<index::Field>(&item)) {
        return slice_value_recursive(project_value_field(value, {field->name}, true), items, position + 1);
    }
    if (const auto* fields = std::get_if<index::Fields>(&item)) {
        return slice_value_recursive(project_value_field(value, fields->names, false), items, position + 1);
    }
    if (std::holds_alternative<index::NewAxis>(item)) {
        return Value::list_type{slice_value_recursive(value, items, position + 1)};
    }
    if (value.is_none()) return Value(nullptr);
    const auto* values = std::get_if<Value::list_type>(&value.storage());
    if (!values) throw std::invalid_argument("slice index is deeper than an input value");

    if (const auto* integer = std::get_if<index::Integer>(&item)) {
        const auto selected = index::detail::normalize_integer(integer->value, values->size(), "slice");
        return slice_value_recursive((*values)[selected], items, position + 1);
    }

    std::vector<std::size_t> selected;
    if (const auto* slice = std::get_if<index::Slice>(&item)) {
        selected = index::detail::indices_for_slice(*slice, values->size());
    } else if (const auto* integers = std::get_if<index::IntegerArray>(&item)) {
        selected = index::detail::normalize_integer_array(integers->values, values->size(), "slice");
    } else if (const auto* booleans = std::get_if<index::BooleanArray>(&item)) {
        if (booleans->values.size() != values->size()) {
            throw std::invalid_argument("boolean mask length must match the selected dimension");
        }
        for (std::size_t i = 0; i < booleans->values.size(); ++i) if (booleans->values[i]) selected.push_back(i);
    } else {
        throw std::invalid_argument("array-backed masks are only supported as standalone indexes");
    }

    Value::list_type result;
    result.reserve(selected.size());
    for (const auto selected_index : selected) {
        result.push_back(slice_value_recursive((*values)[selected_index], items, position + 1));
    }
    return result;
}

inline std::vector<std::ptrdiff_t> row_indices_from_item(const Content& layout, const index::Item& item) {
    if (const auto* integer = std::get_if<index::Integer>(&item)) {
        return {integer->value};
    }
    if (const auto* integers = std::get_if<index::IntegerArray>(&item)) {
        return integers->values;
    }
    if (const auto* slice = std::get_if<index::Slice>(&item)) {
        const auto indices = index::detail::indices_for_slice(*slice, layout.length());
        std::vector<std::ptrdiff_t> result;
        result.reserve(indices.size());
        for (const auto index : indices) {
            result.push_back(static_cast<std::ptrdiff_t>(index));
        }
        return result;
    }
    throw std::invalid_argument("nested slicing requires row integer, integer-array, or range indexing");
}

inline std::shared_ptr<const Content> slice_layout(const Content& layout, const std::vector<index::Item>& items) {
    const auto normalized = normalize_slice_items(layout, items);
    if (normalized.empty()) {
        return layout.to_packed();
    }
    const auto array_mask = std::find_if(normalized.begin(), normalized.end(), [](const auto& item) {
        return std::holds_alternative<index::ArrayIndex>(item);
    });
    if (array_mask != normalized.end() && normalized.size() > 1) {
        const auto& mask_item = std::get<index::ArrayIndex>(*array_mask);
        if (!mask_item.layout) throw std::invalid_argument("array index layout must not be null");
        const std::vector<index::Item> prefix(normalized.begin(), array_mask);
        const std::vector<index::Item> suffix(std::next(array_mask), normalized.end());
        auto selected = prefix.empty() ? layout.to_packed() : slice_layout(layout, prefix);
        auto selected_mask = prefix.empty() ? mask_item.layout->to_packed() : slice_layout(*mask_item.layout, prefix);
        auto masked_layout = selected->mask_as_array(*selected_mask);
        return suffix.empty() ? masked_layout : slice_layout(*masked_layout, suffix);
    }
    const auto generic_nested = normalized.size() > 1 &&
                                (layout.kind() == LayoutKind::indexed ||
                                 layout.kind() == LayoutKind::indexed_option ||
                                 layout.kind() == LayoutKind::byte_masked ||
                                 layout.kind() == LayoutKind::bit_masked ||
                                 layout.kind() == LayoutKind::unmasked ||
                                 layout.kind() == LayoutKind::union_);
    if (normalized.size() > 2 || generic_nested) {
        auto result = slice_value_recursive(layout.to_list(), normalized, 0);
        if (!std::holds_alternative<Value::list_type>(result.storage())) {
            result = Value::list_type{std::move(result)};
        }
        return layout_from_list(result.as_list());
    }

    const auto& first = normalized.front();
    if (normalized.size() == 1) {
        if (std::holds_alternative<index::NewAxis>(first)) {
            return add_outer_axis(layout);
        }
        if (const auto* field = std::get_if<index::Field>(&first)) {
            return layout.field(field->name);
        }
        if (const auto* fields = std::get_if<index::Fields>(&first)) {
            return layout.project_fields(fields->names);
        }
        if (const auto* integer = std::get_if<index::Integer>(&first)) {
            return layout.take_rows({integer->value});
        }
        if (const auto* slice = std::get_if<index::Slice>(&first)) {
            return layout.slice_rows(*slice);
        }
        if (const auto* integers = std::get_if<index::IntegerArray>(&first)) {
            return layout.take_rows(integers->values);
        }
        if (const auto* booleans = std::get_if<index::BooleanArray>(&first)) {
            return layout.mask_rows(booleans->values);
        }
        if (const auto* array_index = std::get_if<index::ArrayIndex>(&first)) {
            if (!array_index->layout) {
                throw std::invalid_argument("array index layout must not be null");
            }
            return layout.mask_as_array(*array_index->layout);
        }
        throw std::invalid_argument("unsupported slice item");
    }

    const auto& second = normalized[1];
    if (std::holds_alternative<index::NewAxis>(first)) {
        if (const auto* integer = std::get_if<index::Integer>(&second)) {
            return layout.take_rows({integer->value});
        }
        if (const auto* slice = std::get_if<index::Slice>(&second)) {
            return add_outer_axis(*layout.slice_rows(*slice));
        }
        if (const auto* integers = std::get_if<index::IntegerArray>(&second)) {
            return add_outer_axis(*layout.take_rows(integers->values));
        }
        throw std::invalid_argument("newaxis inner indexing requires an integer, integer array, or range");
    }
    if (std::holds_alternative<index::NewAxis>(second)) {
        return add_singleton_axis(slice_layout(layout, {first}));
    }
    if (const auto* field = std::get_if<index::Field>(&second)) {
        return slice_layout(*slice_layout(layout, {first}), {*field});
    }
    if (const auto* fields = std::get_if<index::Fields>(&second)) {
        return slice_layout(*slice_layout(layout, {first}), {*fields});
    }
    if (const auto* column_slice = std::get_if<index::Slice>(&second)) {
        index::Slice row_slice;
        if (const auto* row_integer = std::get_if<index::Integer>(&first)) {
            return layout.slice_one_inner(row_integer->value, *column_slice);
        }
        if (const auto* first_slice = std::get_if<index::Slice>(&first)) {
            row_slice = *first_slice;
        } else {
            const auto rows = row_indices_from_item(layout, first);
            return layout.take_rows(rows)->slice_inner(index::Slice{}, *column_slice);
        }
        return layout.slice_inner(row_slice, *column_slice);
    }
    if (const auto* column = std::get_if<index::Integer>(&second)) {
        return layout.take_inner(row_indices_from_item(layout, first), column->value);
    }
    if (const auto* columns = std::get_if<index::IntegerArray>(&second)) {
        if (const auto* row = std::get_if<index::Integer>(&first)) {
            return layout.take_one_inner_array(row->value, columns->values);
        }
        return layout.take_inner_array(row_indices_from_item(layout, first), columns->values);
    }
    throw std::invalid_argument("inner indexing supports integer, integer-array, and range columns");
}

}  // namespace detail

class Record {
public:
    explicit Record(Value value) : value_(std::move(value)) {
        if (!std::holds_alternative<Value::record_type>(value_.storage())) {
            throw std::invalid_argument("ak::Record requires a record value");
        }
    }

    const Value& to_list() const noexcept {
        return value_;
    }

    std::vector<std::string> fields() const {
        return value_.as_record().fields;
    }

    bool is_tuple() const noexcept {
        return value_.as_record().is_tuple;
    }

    Value field(const std::string& name) const {
        const auto& record = value_.as_record();
        for (std::size_t i = 0; i < record.fields.size(); ++i) {
            if (record.fields[i] == name) {
                return record.values[i];
            }
        }
        throw std::out_of_range("record field does not exist: " + name);
    }

private:
    Value value_;
};

class Scalar {
public:
    explicit Scalar(Value value) : value_(std::move(value)), content_type_(detail::scalar_type_from_value(value_)) {}

    const Value& value() const noexcept {
        return value_;
    }

    ScalarType type() const {
        return ScalarType(content_type_);
    }

    std::string typestr() const {
        return content_type_->typestr();
    }

    std::size_t ndim() const noexcept {
        return content_type_->ndim();
    }

    void show(std::ostream& stream) const {
        stream << value_ << '\n';
    }

    operator const Value&() const noexcept {
        return value_;
    }

    friend bool operator==(const Scalar& left, const Value& right) {
        return left.value_ == right;
    }

    friend bool operator==(const Value& left, const Scalar& right) {
        return left == right.value_;
    }

private:
    Value value_;
    TypePtr content_type_;
};

template <typename T>
class ArrayView {
public:
    explicit ArrayView(std::shared_ptr<const Content> owner) requires (!std::same_as<T, bool>)
        : owner_(std::move(owner)), typed_(std::dynamic_pointer_cast<const NumpyArray<T>>(owner_)) {
        if (!typed_) throw std::invalid_argument("ak::Array layout does not match the requested typed view");
    }

    std::size_t size() const noexcept { return typed_->length(); }
    bool empty() const noexcept { return size() == 0; }
    const T* begin() const noexcept { return typed_->values().data(); }
    const T* end() const noexcept { return typed_->values().data() + typed_->values().size(); }
    const T& operator[](std::size_t index) const noexcept { return typed_->values()[index]; }

    const T& at(std::size_t index) const {
        if (index >= size()) throw std::out_of_range("ak::ArrayView index is out of range");
        return typed_->values()[index];
    }

    std::span<const T> values() const noexcept { return typed_->values(); }

private:
    std::shared_ptr<const Content> owner_;
    std::shared_ptr<const NumpyArray<T>> typed_;
};

class Array {
public:
    using Metadata = std::map<std::string, std::string>;
    using NamedAxes = std::map<std::string, int>;

    Array() : layout_(std::make_shared<EmptyArray>()) {}

    explicit Array(std::shared_ptr<const Content> layout,
                   Metadata behavior = {},
                   Metadata attrs = {},
                   NamedAxes named_axes = {})
        : layout_(std::move(layout)),
          behavior_(std::move(behavior)),
          attrs_(std::move(attrs)),
          named_axes_(std::move(named_axes)) {
        if (!layout_) {
            throw std::invalid_argument("ak::Array layout must not be null");
        }
        const auto error = layout_->validity_error();
        if (!error.empty()) {
            throw std::invalid_argument(error);
        }
        const auto dimensions = static_cast<int>(ndim());
        for (const auto& [name, axis] : named_axes_) {
            if (name.empty()) {
                throw std::invalid_argument("ak::Array named-axis names must not be empty");
            }
            if (axis < -dimensions || axis >= dimensions) {
                throw std::invalid_argument("ak::Array named axis is outside the array dimensions");
            }
        }
    }

    const Content& layout() const noexcept {
        return *layout_;
    }

    std::shared_ptr<const Content> layout_ptr() const noexcept {
        return layout_;
    }

    const Metadata& behavior() const noexcept {
        return behavior_;
    }

    const Metadata& attrs() const noexcept {
        return attrs_;
    }

    const NamedAxes& named_axes() const noexcept {
        return named_axes_;
    }

    Array with_layout(std::shared_ptr<const Content> layout) const {
        return Array(std::move(layout), behavior_, attrs_, named_axes_);
    }

    Array with_behavior(Metadata behavior) const {
        return Array(layout_, std::move(behavior), attrs_, named_axes_);
    }

    Array with_attrs(Metadata attrs) const {
        return Array(layout_, behavior_, std::move(attrs), named_axes_);
    }

    Array with_named_axes(NamedAxes named_axes) const {
        return Array(layout_, behavior_, attrs_, std::move(named_axes));
    }

    std::size_t length() const noexcept {
        return layout_->length();
    }

    std::size_t nbytes() const noexcept {
        return layout_->nbytes();
    }

    ArrayType type() const {
        detail::BufferBuilder builder;
        return ArrayType(detail::type_from_form(layout_->to_buffers(builder)), length());
    }

    std::string typestr() const {
        return layout_->typestr();
    }

    std::size_t ndim() const {
        return type().ndim();
    }

    std::string validity_error() const {
        return layout_->validity_error();
    }

    bool is_valid() const {
        return validity_error().empty();
    }

    Value to_list() const {
        return layout_->to_list();
    }

    Value at(std::ptrdiff_t index) const {
        return layout_->at(index);
    }

    Value at(std::ptrdiff_t outer, std::ptrdiff_t inner) const {
        return layout_->at(outer, inner);
    }

    Record record_at(std::ptrdiff_t index) const {
        return Record(at(index));
    }

    Scalar scalar_at(std::ptrdiff_t index) const {
        return Scalar(at(index));
    }

    template <typename T>
    ArrayView<T> view() const requires (!std::same_as<T, bool>) {
        return ArrayView<T>(layout_);
    }

    std::vector<std::string> fields() const {
        return layout_->fields();
    }

    bool is_tuple() const noexcept {
        return layout_->is_tuple();
    }

    Array field(const std::string& name) const {
        return with_layout(layout_->field(name));
    }

    Array project_fields(const std::vector<std::string>& names) const {
        return with_layout(layout_->project_fields(names));
    }

    Array slice(std::initializer_list<index::Item> items) const {
        return with_layout(detail::slice_layout(*layout_, std::vector<index::Item>(items.begin(), items.end())));
    }

    Array slice(const std::vector<index::Item>& items) const {
        return with_layout(detail::slice_layout(*layout_, items));
    }

    void show(std::ostream& stream) const {
        stream << to_list() << '\n';
    }

private:
    std::shared_ptr<const Content> layout_;
    Metadata behavior_;
    Metadata attrs_;
    NamedAxes named_axes_;
};

using ReducerResult = std::variant<Scalar, Array>;

}  // namespace ak

namespace ak {

class ListArray final : public Content {
public:
    ListArray(std::vector<std::size_t> starts,
              std::vector<std::size_t> stops,
              std::shared_ptr<const Content> content)
        : starts_(std::move(starts)), stops_(std::move(stops)), content_(std::move(content)) {
        const auto error = validate();
        if (!error.empty()) throw std::invalid_argument(error);
    }

    LayoutKind kind() const noexcept override { return LayoutKind::list; }
    std::size_t length() const noexcept override { return starts_.size(); }

    std::size_t nbytes() const noexcept override {
        return (starts_.size() + stops_.size()) * sizeof(std::size_t) + content_->nbytes();
    }

    std::string typestr() const override {
        return detail::list_type_string(length(), detail::item_type_from_typestr(content_->typestr()));
    }

    std::string validity_error() const override { return validate(); }

    Value to_list() const override {
        Value::list_type result;
        result.reserve(length());
        for (std::size_t row = 0; row < length(); ++row) result.push_back(row_value(row));
        return result;
    }

    Value at(std::ptrdiff_t index) const override {
        return row_value(index::detail::normalize_integer(index, length(), "row"));
    }

    Value at(std::ptrdiff_t outer, std::ptrdiff_t inner) const override {
        const auto row = index::detail::normalize_integer(outer, length(), "row");
        const auto column = index::detail::normalize_integer(inner, stops_[row] - starts_[row], "column");
        return content_->at(static_cast<std::ptrdiff_t>(starts_[row] + column));
    }

    std::shared_ptr<const Content> slice_rows(const index::Slice& slice) const override {
        return select(index::detail::indices_for_slice(slice, length()));
    }

    std::shared_ptr<const Content> take_rows(const std::vector<std::ptrdiff_t>& indices) const override {
        return select(index::detail::normalize_integer_array(indices, length(), "row"));
    }

    std::shared_ptr<const Content> mask_rows(const std::vector<bool>& mask) const override {
        if (mask.size() != length()) throw std::invalid_argument("boolean mask length must match row count");
        std::vector<std::size_t> selected;
        for (std::size_t i = 0; i < mask.size(); ++i) if (mask[i]) selected.push_back(i);
        return select(selected);
    }

    std::shared_ptr<const Content> mask_as_array(const Content& mask) const override {
        const auto* values = mask.flat_bool_mask();
        if (!values) throw std::invalid_argument("list array requires a flat boolean mask");
        return mask_rows(*values);
    }

    std::shared_ptr<const Content> slice_inner(const index::Slice& rows,
                                               const index::Slice& columns) const override {
        return select_inner_slice(index::detail::indices_for_slice(rows, length()), columns, false);
    }

    std::shared_ptr<const Content> slice_one_inner(std::ptrdiff_t row,
                                                   const index::Slice& columns) const override {
        return select_inner_slice({index::detail::normalize_integer(row, length(), "row")}, columns, true);
    }

    std::shared_ptr<const Content> take_inner(const std::vector<std::ptrdiff_t>& rows,
                                              std::ptrdiff_t column) const override {
        const std::vector<std::ptrdiff_t> columns{column};
        return select_inner_array(index::detail::normalize_integer_array(rows, length(), "row"), columns, false)
            ->flatten();
    }

    std::shared_ptr<const Content> take_inner_array(const std::vector<std::ptrdiff_t>& rows,
                                                    const std::vector<std::ptrdiff_t>& columns) const override {
        return select_inner_array(index::detail::normalize_integer_array(rows, length(), "row"), columns, false);
    }

    std::shared_ptr<const Content> take_one_inner_array(
        std::ptrdiff_t row, const std::vector<std::ptrdiff_t>& columns) const override {
        return select_inner_array({index::detail::normalize_integer(row, length(), "row")}, columns, true);
    }

    std::vector<std::string> fields() const override { return content_->fields(); }
    bool is_tuple() const noexcept override { return content_->is_tuple(); }
    std::string record_name() const override { return content_->record_name(); }

    std::shared_ptr<const Content> field(const std::string& name) const override {
        return std::make_shared<ListArray>(starts_, stops_, content_->field(name));
    }

    std::shared_ptr<const Content> project_fields(const std::vector<std::string>& names) const override {
        return std::make_shared<ListArray>(starts_, stops_, content_->project_fields(names));
    }

    std::shared_ptr<const Content> with_name(std::string name) const override {
        return std::make_shared<ListArray>(starts_, stops_, content_->with_name(std::move(name)));
    }

    std::vector<std::size_t> num() const override {
        std::vector<std::size_t> result;
        result.reserve(length());
        for (std::size_t i = 0; i < length(); ++i) result.push_back(stops_[i] - starts_[i]);
        return result;
    }

    std::shared_ptr<const Content> flatten() const override {
        return content_->take_rows(flat_indices())->to_packed();
    }

    std::shared_ptr<const Content> to_packed() const override {
        auto indices = flat_indices();
        std::vector<std::size_t> offsets;
        offsets.reserve(length() + 1);
        offsets.push_back(0);
        for (std::size_t row = 0; row < length(); ++row) offsets.push_back(offsets.back() + stops_[row] - starts_[row]);
        return std::make_shared<ListOffsetContentArray>(content_->take_rows(indices)->to_packed(), std::move(offsets));
    }

    std::shared_ptr<const Content> local_index() const override {
        std::vector<std::int64_t> values;
        std::vector<std::size_t> offsets;
        offsets.reserve(length() + 1);
        offsets.push_back(0);
        for (std::size_t row = 0; row < length(); ++row) {
            for (std::size_t i = starts_[row]; i < stops_[row]; ++i) values.push_back(static_cast<std::int64_t>(i - starts_[row]));
            offsets.push_back(values.size());
        }
        return std::make_shared<ListOffsetContentArray>(
            std::make_shared<NumpyArray<std::int64_t>>(std::move(values)), std::move(offsets));
    }

    const std::vector<std::size_t>& starts() const noexcept { return starts_; }
    const std::vector<std::size_t>& stops() const noexcept { return stops_; }
    const Content& content() const noexcept { return *content_; }

    Form to_buffers(detail::BufferBuilder& builder) const override {
        auto key = builder.next_key();
        builder.add(key + "-starts", detail::index_buffer_from_offsets(starts_));
        builder.add(key + "-stops", detail::index_buffer_from_offsets(stops_));
        auto content_form = content_->to_buffers(builder);
        return Form{
            .kind = FormKind::list,
            .key = std::move(key),
            .contents = {std::move(content_form)},
            .length = length(),
        };
    }

private:
    std::shared_ptr<const Content> select_inner_slice(const std::vector<std::size_t>& rows,
                                                      const index::Slice& columns,
                                                      bool drop_outer) const {
        std::vector<std::ptrdiff_t> selected;
        std::vector<std::size_t> offsets;
        offsets.reserve(rows.size() + 1);
        offsets.push_back(0);
        for (const auto row : rows) {
            for (const auto column :
                 index::detail::indices_for_slice(columns, stops_[row] - starts_[row])) {
                selected.push_back(static_cast<std::ptrdiff_t>(starts_[row] + column));
            }
            offsets.push_back(selected.size());
        }
        auto content = std::make_shared<IndexedArray>(std::move(selected), content_);
        if (drop_outer) return content;
        return std::make_shared<ListOffsetContentArray>(std::move(content), std::move(offsets));
    }

    std::shared_ptr<const Content> select_inner_array(const std::vector<std::size_t>& rows,
                                                      const std::vector<std::ptrdiff_t>& columns,
                                                      bool drop_outer) const {
        std::vector<std::ptrdiff_t> selected;
        std::vector<std::size_t> offsets;
        offsets.reserve(rows.size() + 1);
        offsets.push_back(0);
        for (const auto row : rows) {
            for (const auto column :
                 index::detail::normalize_integer_array(columns, stops_[row] - starts_[row], "column")) {
                selected.push_back(static_cast<std::ptrdiff_t>(starts_[row] + column));
            }
            offsets.push_back(selected.size());
        }
        auto content = std::make_shared<IndexedArray>(std::move(selected), content_);
        if (drop_outer) return content;
        return std::make_shared<ListOffsetContentArray>(std::move(content), std::move(offsets));
    }

    Value row_value(std::size_t row) const {
        Value::list_type result;
        result.reserve(stops_[row] - starts_[row]);
        for (std::size_t i = starts_[row]; i < stops_[row]; ++i) result.push_back(content_->at(static_cast<std::ptrdiff_t>(i)));
        return result;
    }

    std::vector<std::ptrdiff_t> flat_indices() const {
        std::vector<std::ptrdiff_t> result;
        for (std::size_t row = 0; row < length(); ++row) {
            for (std::size_t i = starts_[row]; i < stops_[row]; ++i) result.push_back(static_cast<std::ptrdiff_t>(i));
        }
        return result;
    }

    std::shared_ptr<const Content> select(const std::vector<std::size_t>& rows) const {
        std::vector<std::size_t> starts;
        std::vector<std::size_t> stops;
        starts.reserve(rows.size());
        stops.reserve(rows.size());
        for (const auto row : rows) {
            starts.push_back(starts_[row]);
            stops.push_back(stops_[row]);
        }
        return std::make_shared<ListArray>(std::move(starts), std::move(stops), content_);
    }

    std::string validate() const {
        if (!content_) return "ak::ListArray content must not be null";
        const auto error = content_->validity_error();
        if (!error.empty()) return error;
        if (starts_.size() != stops_.size()) return "ak::ListArray starts and stops must have matching lengths";
        for (std::size_t i = 0; i < starts_.size(); ++i) {
            if (starts_[i] > stops_[i]) return "ak::ListArray start must not exceed stop";
            if (stops_[i] > content_->length()) return "ak::ListArray stop must not exceed content length";
        }
        return {};
    }

    std::vector<std::size_t> starts_;
    std::vector<std::size_t> stops_;
    std::shared_ptr<const Content> content_;
};

}  // namespace ak

namespace ak {

template <typename T>
class ListOffsetArray final : public Content {
public:
    using value_type = T;

    ListOffsetArray(std::shared_ptr<const NumpyArray<T>> content, std::vector<std::size_t> offsets)
        : content_(std::move(content)), offsets_(std::move(offsets)) {
        const auto error = validate();
        if (!error.empty()) {
            throw std::invalid_argument(error);
        }
    }

    ListOffsetArray(std::vector<T> values, std::vector<std::size_t> offsets)
        : ListOffsetArray(std::make_shared<NumpyArray<T>>(std::move(values)), std::move(offsets)) {}

    LayoutKind kind() const noexcept override {
        return LayoutKind::list_offset;
    }

    std::size_t length() const noexcept override {
        return offsets_.size() - 1;
    }

    std::size_t nbytes() const noexcept override {
        return content_->nbytes() + offsets_.size() * sizeof(std::size_t);
    }

    std::string typestr() const override {
        return detail::list_type_string(length(), detail::primitive_type_name<T>());
    }

    std::string validity_error() const override {
        return validate();
    }

    Value to_list() const override {
        Value::list_type rows;
        rows.reserve(length());
        for (std::size_t row = 0; row < length(); ++row) {
            const auto start = offsets_[row];
            const auto stop = offsets_[row + 1];
            Value::list_type values;
            values.reserve(stop - start);
            for (auto i = start; i < stop; ++i) {
                values.emplace_back(detail::scalar_to_value(content_->values_vector()[i]));
            }
            rows.emplace_back(std::move(values));
        }
        return rows;
    }

    bool ragged_bool_mask(std::vector<bool>& values, std::vector<std::size_t>& offsets) const override {
        if constexpr (std::same_as<T, bool>) {
            values = content_->values_vector();
            offsets = offsets_;
            return true;
        } else {
            (void)values;
            (void)offsets;
            return false;
        }
    }

    Value at(std::ptrdiff_t index) const override {
        const auto row = index::detail::normalize_integer(index, length(), "row");
        return row_value(row);
    }

    Value at(std::ptrdiff_t outer, std::ptrdiff_t inner) const override {
        const auto row = index::detail::normalize_integer(outer, length(), "row");
        const auto start = offsets_[row];
        const auto stop = offsets_[row + 1];
        const auto column = index::detail::normalize_integer(inner, stop - start, "column");
        return detail::scalar_to_value(content_->values_vector()[start + column]);
    }

    std::shared_ptr<const Content> slice_rows(const index::Slice& slice) const override {
        return select_rows(index::detail::indices_for_slice(slice, length()));
    }

    std::shared_ptr<const Content> take_rows(const std::vector<std::ptrdiff_t>& indices) const override {
        return select_rows(index::detail::normalize_integer_array(indices, length(), "row"));
    }

    std::shared_ptr<const Content> mask_rows(const std::vector<bool>& mask) const override {
        return select_rows(rows_from_mask(mask));
    }

    std::shared_ptr<const Content> mask_as_array(const Content& mask) const override {
        if (const auto* flat_mask = mask.flat_bool_mask()) {
            return mask_rows(*flat_mask);
        }
        std::vector<bool> mask_values;
        std::vector<std::size_t> mask_offsets;
        if (mask.ragged_bool_mask(mask_values, mask_offsets)) {
            return apply_ragged_mask(mask_values, mask_offsets);
        }
        throw std::invalid_argument("array indexing requires a boolean mask");
    }

    std::shared_ptr<const Content> slice_inner(const index::Slice& rows, const index::Slice& columns) const override {
        return select_inner_slice(index::detail::indices_for_slice(rows, length()), columns);
    }

    std::shared_ptr<const Content> slice_one_inner(std::ptrdiff_t row, const index::Slice& columns) const override {
        const auto row_index = index::detail::normalize_integer(row, length(), "row");
        const auto start = offsets_[row_index];
        const auto stop = offsets_[row_index + 1];
        std::vector<T> values;
        for (const auto column : index::detail::indices_for_slice(columns, stop - start)) {
            values.push_back(content_->values_vector()[start + column]);
        }
        return std::make_shared<NumpyArray<T>>(std::move(values));
    }

    std::shared_ptr<const Content> take_inner(const std::vector<std::ptrdiff_t>& rows,
                                              std::ptrdiff_t column) const override {
        return select_inner_integer(index::detail::normalize_integer_array(rows, length(), "row"), column);
    }

    std::shared_ptr<const Content> take_inner_array(const std::vector<std::ptrdiff_t>& rows,
                                                    const std::vector<std::ptrdiff_t>& columns) const override {
        return select_inner_array(index::detail::normalize_integer_array(rows, length(), "row"), columns, false);
    }

    std::shared_ptr<const Content> take_one_inner_array(
        std::ptrdiff_t row, const std::vector<std::ptrdiff_t>& columns) const override {
        return select_inner_array({index::detail::normalize_integer(row, length(), "row")}, columns, true);
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
        if (normalized_items.size() == 1) {
            if (const auto* array_index = std::get_if<index::ArrayIndex>(&normalized_items.front())) {
                std::vector<bool> mask_values;
                std::vector<std::size_t> mask_offsets;
                if (array_index->layout->ragged_bool_mask(mask_values, mask_offsets)) {
                    return apply_ragged_mask(mask_values, mask_offsets);
                }
            }
        }
        if (normalized_items.size() > 2) {
            throw std::invalid_argument("low-level list-offset slicing supports at most row and column items");
        }

        const auto rows = rows_from_item(normalized_items.front());
        if (rows.dropped_dimension && normalized_items.size() == 1) {
            return make_row_content(rows.indices.front());
        }
        if (normalized_items.size() == 1) {
            return select_rows(rows.indices);
        }

        const auto& inner = normalized_items[1];
        if (const auto* integer = std::get_if<index::Integer>(&inner)) {
            return select_inner_integer(rows.indices, integer->value);
        }
        if (const auto* slice = std::get_if<index::Slice>(&inner)) {
            return select_inner_slice(rows.indices, *slice);
        }
        if (const auto* columns = std::get_if<index::IntegerArray>(&inner)) {
            return select_inner_array(rows.indices, columns->values, rows.dropped_dimension);
        }
        throw std::invalid_argument("unsupported inner slice item");
    }

    std::vector<std::size_t> num() const override {
        std::vector<std::size_t> result;
        result.reserve(length());
        for (std::size_t row = 0; row < length(); ++row) {
            result.push_back(offsets_[row + 1] - offsets_[row]);
        }
        return result;
    }

    std::shared_ptr<const Content> flatten() const override {
        return content_;
    }

    std::shared_ptr<const Content> to_packed() const override {
        return std::make_shared<ListOffsetArray<T>>(content_, offsets_);
    }

    std::shared_ptr<const Content> local_index() const override {
        std::vector<std::int64_t> values;
        std::vector<std::size_t> offsets;
        offsets.reserve(length() + 1);
        offsets.push_back(0);
        for (std::size_t row = 0; row < length(); ++row) {
            const auto row_length = offsets_[row + 1] - offsets_[row];
            for (std::size_t i = 0; i < row_length; ++i) {
                values.push_back(static_cast<std::int64_t>(i));
            }
            offsets.push_back(values.size());
        }
        return std::make_shared<ListOffsetArray<std::int64_t>>(std::move(values), std::move(offsets));
    }

    const NumpyArray<T>& content() const noexcept {
        return *content_;
    }

    std::span<const std::size_t> offsets() const noexcept {
        return std::span<const std::size_t>(offsets_.data(), offsets_.size());
    }

    Form to_buffers(detail::BufferBuilder& builder) const override {
        auto key = builder.next_key();
        builder.add(key + "-offsets", detail::index_buffer_from_offsets(offsets_));
        auto content_form = content_->to_buffers(builder);
        return Form{
            .kind = FormKind::list_offset,
            .key = std::move(key),
            .contents = {std::move(content_form)},
            .length = length(),
        };
    }

private:
    struct RowSelection {
        std::vector<std::size_t> indices;
        bool dropped_dimension{false};
    };

    Value row_value(std::size_t row) const {
        const auto start = offsets_[row];
        const auto stop = offsets_[row + 1];
        Value::list_type values;
        values.reserve(stop - start);
        for (auto i = start; i < stop; ++i) {
            values.emplace_back(detail::scalar_to_value(content_->values_vector()[i]));
        }
        return values;
    }

    RowSelection rows_from_item(const index::Item& item) const {
        if (const auto* integer = std::get_if<index::Integer>(&item)) {
            return RowSelection{{index::detail::normalize_integer(integer->value, length(), "row")}, true};
        }
        if (const auto* slice = std::get_if<index::Slice>(&item)) {
            return RowSelection{index::detail::indices_for_slice(*slice, length()), false};
        }
        if (const auto* integers = std::get_if<index::IntegerArray>(&item)) {
            return RowSelection{index::detail::normalize_integer_array(integers->values, length(), "row"), false};
        }
        if (const auto* booleans = std::get_if<index::BooleanArray>(&item)) {
            return RowSelection{rows_from_mask(booleans->values), false};
        }
        if (const auto* array_index = std::get_if<index::ArrayIndex>(&item)) {
            if (const auto* flat_mask = array_index->layout->flat_bool_mask()) {
                return RowSelection{rows_from_mask(*flat_mask), false};
            }
            throw std::invalid_argument("array indexing requires a boolean mask");
        }
        throw std::invalid_argument("unsupported row slice item");
    }

    std::vector<std::size_t> rows_from_mask(const std::vector<bool>& mask) const {
        if (mask.size() != length()) {
            throw std::invalid_argument("boolean mask length must match row count");
        }
        std::vector<std::size_t> rows;
        for (std::size_t i = 0; i < mask.size(); ++i) {
            if (mask[i]) {
                rows.push_back(i);
            }
        }
        return rows;
    }

    std::shared_ptr<const Content> make_row_content(std::size_t row) const {
        const auto start = offsets_[row];
        const auto stop = offsets_[row + 1];
        std::vector<T> values;
        values.reserve(stop - start);
        values.insert(values.end(), content_->values_vector().begin() + static_cast<std::ptrdiff_t>(start),
                      content_->values_vector().begin() + static_cast<std::ptrdiff_t>(stop));
        return std::make_shared<NumpyArray<T>>(std::move(values));
    }

    std::shared_ptr<const Content> select_rows(const std::vector<std::size_t>& rows) const {
        std::vector<T> values;
        std::vector<std::size_t> offsets;
        offsets.reserve(rows.size() + 1);
        offsets.push_back(0);
        for (const auto row : rows) {
            const auto start = offsets_[row];
            const auto stop = offsets_[row + 1];
            values.insert(values.end(), content_->values_vector().begin() + static_cast<std::ptrdiff_t>(start),
                          content_->values_vector().begin() + static_cast<std::ptrdiff_t>(stop));
            offsets.push_back(values.size());
        }
        return std::make_shared<ListOffsetArray<T>>(std::move(values), std::move(offsets));
    }

    std::shared_ptr<const Content> select_inner_integer(const std::vector<std::size_t>& rows,
                                                        std::ptrdiff_t column_index) const {
        std::vector<T> values;
        values.reserve(rows.size());
        for (const auto row : rows) {
            const auto start = offsets_[row];
            const auto stop = offsets_[row + 1];
            const auto column = index::detail::normalize_integer(column_index, stop - start, "column");
            values.push_back(content_->values_vector()[start + column]);
        }
        return std::make_shared<NumpyArray<T>>(std::move(values));
    }

    std::shared_ptr<const Content> select_inner_slice(const std::vector<std::size_t>& rows,
                                                      const index::Slice& slice) const {
        std::vector<T> values;
        std::vector<std::size_t> offsets;
        offsets.reserve(rows.size() + 1);
        offsets.push_back(0);
        for (const auto row : rows) {
            const auto start = offsets_[row];
            const auto stop = offsets_[row + 1];
            for (const auto index : index::detail::indices_for_slice(slice, stop - start)) {
                values.push_back(content_->values_vector()[start + index]);
            }
            offsets.push_back(values.size());
        }
        return std::make_shared<ListOffsetArray<T>>(std::move(values), std::move(offsets));
    }

    std::shared_ptr<const Content> select_inner_array(const std::vector<std::size_t>& rows,
                                                      const std::vector<std::ptrdiff_t>& columns,
                                                      bool drop_outer) const {
        std::vector<T> values;
        std::vector<std::size_t> offsets;
        offsets.reserve(rows.size() + 1);
        offsets.push_back(0);
        for (const auto row : rows) {
            const auto start = offsets_[row];
            const auto stop = offsets_[row + 1];
            for (const auto column : index::detail::normalize_integer_array(columns, stop - start, "column")) {
                values.push_back(content_->values_vector()[start + column]);
            }
            offsets.push_back(values.size());
        }
        if (drop_outer) return std::make_shared<NumpyArray<T>>(std::move(values));
        return std::make_shared<ListOffsetArray<T>>(std::move(values), std::move(offsets));
    }

    std::shared_ptr<const Content> apply_ragged_mask(const std::vector<bool>& mask_values,
                                                     const std::vector<std::size_t>& mask_offsets) const {
        if (mask_offsets.empty() || mask_offsets.size() - 1 != length()) {
            throw std::invalid_argument("ragged boolean mask row count must match array row count");
        }

        std::vector<T> values;
        std::vector<std::size_t> offsets;
        offsets.reserve(length() + 1);
        offsets.push_back(0);

        for (std::size_t row = 0; row < length(); ++row) {
            const auto start = offsets_[row];
            const auto stop = offsets_[row + 1];
            if (mask_offsets[row + 1] - mask_offsets[row] != stop - start) {
                throw std::invalid_argument("ragged boolean mask row lengths must match array row lengths");
            }
            for (std::size_t i = 0; i < stop - start; ++i) {
                if (mask_values[mask_offsets[row] + i]) {
                    values.push_back(content_->values_vector()[start + i]);
                }
            }
            offsets.push_back(values.size());
        }

        return std::make_shared<ListOffsetArray<T>>(std::move(values), std::move(offsets));
    }

    std::string validate() const {
        if (!content_) {
            return "ak::ListOffsetArray content must not be null";
        }
        if (offsets_.empty()) {
            return "ak::ListOffsetArray offsets must contain at least the initial zero offset";
        }
        if (offsets_.front() != 0) {
            return "ak::ListOffsetArray offsets must start at zero";
        }
        if (offsets_.back() != content_->length()) {
            return "ak::ListOffsetArray final offset must equal content length";
        }
        if (!std::is_sorted(offsets_.begin(), offsets_.end())) {
            return "ak::ListOffsetArray offsets must be monotonic";
        }
        return {};
    }

    std::shared_ptr<const NumpyArray<T>> content_;
    std::vector<std::size_t> offsets_;
};

template <typename T>
std::shared_ptr<const Content> NumpyArray<T>::unflatten(const std::vector<std::size_t>& counts) const {
    std::vector<std::size_t> offsets;
    offsets.reserve(counts.size() + 1);
    offsets.push_back(0);

    std::size_t total = 0;
    for (const auto count : counts) {
        total += count;
        offsets.push_back(total);
    }
    if (total != values_.size()) {
        throw std::invalid_argument("ak::unflatten counts must sum to array length");
    }

    return std::make_shared<ListOffsetArray<T>>(std::make_shared<NumpyArray<T>>(values_), std::move(offsets));
}

}  // namespace ak

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

namespace ak {

template <typename T>
class RegularArray final : public Content {
public:
    using value_type = T;

    RegularArray(std::shared_ptr<const NumpyArray<T>> content,
                 std::size_t size,
                 std::optional<std::size_t> length = std::nullopt)
        : content_(std::move(content)),
          size_(size),
          length_(length.value_or(size == 0 || !content_ ? 0 : content_->length() / size)) {
        const auto error = validate();
        if (!error.empty()) {
            throw std::invalid_argument(error);
        }
    }

    RegularArray(std::vector<T> values, std::size_t size, std::optional<std::size_t> length = std::nullopt)
        : RegularArray(std::make_shared<NumpyArray<T>>(std::move(values)), size, length) {}

    LayoutKind kind() const noexcept override {
        return LayoutKind::regular;
    }

    std::size_t length() const noexcept override {
        return length_;
    }

    std::size_t nbytes() const noexcept override {
        return content_->nbytes();
    }

    std::string typestr() const override {
        return std::to_string(length()) + " * " + std::to_string(size_) + " * " +
               detail::primitive_type_name<T>();
    }

    std::string validity_error() const override {
        return validate();
    }

    Value to_list() const override {
        Value::list_type rows;
        rows.reserve(length());
        const auto& values = content_->values_vector();
        for (std::size_t row = 0; row < length(); ++row) {
            Value::list_type items;
            items.reserve(size_);
            const auto start = row * size_;
            for (std::size_t i = 0; i < size_; ++i) {
                items.emplace_back(detail::scalar_to_value(values[start + i]));
            }
            rows.emplace_back(std::move(items));
        }
        return rows;
    }

    Value at(std::ptrdiff_t index) const override {
        return as_list_offset().at(index);
    }

    Value at(std::ptrdiff_t outer, std::ptrdiff_t inner) const override {
        const auto row = index::detail::normalize_integer(outer, length(), "row");
        const auto column = index::detail::normalize_integer(inner, size_, "column");
        return detail::scalar_to_value(content_->values_vector()[row * size_ + column]);
    }

    std::shared_ptr<const Content> slice_rows(const index::Slice& slice) const override {
        return as_list_offset().slice_rows(slice);
    }

    std::shared_ptr<const Content> take_rows(const std::vector<std::ptrdiff_t>& indices) const override {
        return as_list_offset().take_rows(indices);
    }

    std::shared_ptr<const Content> mask_rows(const std::vector<bool>& mask) const override {
        return as_list_offset().mask_rows(mask);
    }

    std::shared_ptr<const Content> mask_as_array(const Content& mask) const override {
        return as_list_offset().mask_as_array(mask);
    }

    std::shared_ptr<const Content> slice_inner(const index::Slice& rows, const index::Slice& columns) const override {
        return as_list_offset().slice_inner(rows, columns);
    }

    std::shared_ptr<const Content> slice_one_inner(std::ptrdiff_t row, const index::Slice& columns) const override {
        return as_list_offset().slice_one_inner(row, columns);
    }

    std::shared_ptr<const Content> take_inner(const std::vector<std::ptrdiff_t>& rows,
                                              std::ptrdiff_t column) const override {
        return as_list_offset().take_inner(rows, column);
    }

    std::shared_ptr<const Content> take_inner_array(const std::vector<std::ptrdiff_t>& rows,
                                                    const std::vector<std::ptrdiff_t>& columns) const override {
        return as_list_offset().take_inner_array(rows, columns);
    }

    std::shared_ptr<const Content> take_one_inner_array(
        std::ptrdiff_t row, const std::vector<std::ptrdiff_t>& columns) const override {
        return as_list_offset().take_one_inner_array(row, columns);
    }

    std::shared_ptr<const Content> slice_items(const std::vector<index::Item>& items) const {
        return as_list_offset().slice_items(items);
    }

    std::vector<std::size_t> num() const override {
        return std::vector<std::size_t>(length(), size_);
    }

    std::shared_ptr<const Content> flatten() const override {
        return content_;
    }

    std::shared_ptr<const Content> to_packed() const override {
        return std::make_shared<RegularArray<T>>(content_, size_, length_);
    }

    std::shared_ptr<const Content> local_index() const override {
        std::vector<std::int64_t> values;
        values.reserve(content_->length());
        for (std::size_t row = 0; row < length(); ++row) {
            for (std::size_t i = 0; i < size_; ++i) {
                values.push_back(static_cast<std::int64_t>(i));
            }
        }
        return std::make_shared<RegularArray<std::int64_t>>(std::move(values), size_, length_);
    }

    std::size_t size() const noexcept {
        return size_;
    }

    const NumpyArray<T>& content() const noexcept {
        return *content_;
    }

    Form to_buffers(detail::BufferBuilder& builder) const override {
        auto key = builder.next_key();
        auto content_form = content_->to_buffers(builder);
        return Form{
            .kind = FormKind::regular,
            .key = std::move(key),
            .contents = {std::move(content_form)},
            .length = length(),
            .size = size_,
        };
    }

private:
    ListOffsetArray<T> as_list_offset() const {
        std::vector<std::size_t> offsets;
        offsets.reserve(length() + 1);
        offsets.push_back(0);
        for (std::size_t row = 0; row < length(); ++row) {
            offsets.push_back((row + 1) * size_);
        }
        return ListOffsetArray<T>(content_, std::move(offsets));
    }

    std::string validate() const {
        if (!content_) {
            return "ak::RegularArray content must not be null";
        }
        if (size_ == 0) {
            return content_->length() == 0 ? std::string{} :
                                             "ak::RegularArray size zero requires empty content";
        }
        if (length_ > std::numeric_limits<std::size_t>::max() / size_ || length_ * size_ != content_->length()) {
            return "ak::RegularArray content length must equal length times regular size";
        }
        return {};
    }

    std::shared_ptr<const NumpyArray<T>> content_;
    std::size_t size_;
    std::size_t length_;
};

}  // namespace ak

namespace ak {

class RegularContentArray final : public Content {
public:
    RegularContentArray(std::shared_ptr<const Content> content,
                        std::size_t size,
                        std::optional<std::size_t> length = std::nullopt)
        : content_(std::move(content)),
          size_(size),
          length_(length.value_or(size == 0 || !content_ ? 0 : content_->length() / size)) {
        const auto error = validate();
        if (!error.empty()) throw std::invalid_argument(error);
    }

    LayoutKind kind() const noexcept override { return LayoutKind::regular; }
    std::size_t length() const noexcept override { return length_; }
    std::size_t nbytes() const noexcept override { return content_->nbytes(); }

    std::string typestr() const override {
        return std::to_string(length_) + " * " + std::to_string(size_) + " * " +
               detail::item_type_from_typestr(content_->typestr());
    }

    std::string validity_error() const override { return validate(); }

    Value to_list() const override { return as_list_offset().to_list(); }
    Value at(std::ptrdiff_t index) const override { return as_list_offset().at(index); }
    Value at(std::ptrdiff_t outer, std::ptrdiff_t inner) const override { return as_list_offset().at(outer, inner); }

    std::shared_ptr<const Content> slice_rows(const index::Slice& slice) const override {
        return as_list_offset().slice_rows(slice);
    }
    std::shared_ptr<const Content> take_rows(const std::vector<std::ptrdiff_t>& indices) const override {
        return as_list_offset().take_rows(indices);
    }
    std::shared_ptr<const Content> mask_rows(const std::vector<bool>& mask) const override {
        return as_list_offset().mask_rows(mask);
    }
    std::shared_ptr<const Content> mask_as_array(const Content& mask) const override {
        return as_list_offset().mask_as_array(mask);
    }
    std::shared_ptr<const Content> slice_inner(const index::Slice& rows,
                                               const index::Slice& columns) const override {
        return as_list_offset().slice_inner(rows, columns);
    }
    std::shared_ptr<const Content> slice_one_inner(std::ptrdiff_t row,
                                                   const index::Slice& columns) const override {
        return as_list_offset().slice_one_inner(row, columns);
    }
    std::shared_ptr<const Content> take_inner(const std::vector<std::ptrdiff_t>& rows,
                                              std::ptrdiff_t column) const override {
        return as_list_offset().take_inner(rows, column);
    }
    std::shared_ptr<const Content> take_inner_array(const std::vector<std::ptrdiff_t>& rows,
                                                    const std::vector<std::ptrdiff_t>& columns) const override {
        return as_list_offset().take_inner_array(rows, columns);
    }
    std::shared_ptr<const Content> take_one_inner_array(
        std::ptrdiff_t row, const std::vector<std::ptrdiff_t>& columns) const override {
        return as_list_offset().take_one_inner_array(row, columns);
    }

    std::vector<std::string> fields() const override { return content_->fields(); }
    bool is_tuple() const noexcept override { return content_->is_tuple(); }
    std::string record_name() const override { return content_->record_name(); }
    std::shared_ptr<const Content> field(const std::string& name) const override {
        return std::make_shared<RegularContentArray>(content_->field(name), size_, length_);
    }
    std::shared_ptr<const Content> project_fields(const std::vector<std::string>& names) const override {
        return std::make_shared<RegularContentArray>(content_->project_fields(names), size_, length_);
    }
    std::shared_ptr<const Content> with_name(std::string name) const override {
        return std::make_shared<RegularContentArray>(content_->with_name(std::move(name)), size_, length_);
    }

    std::vector<std::size_t> num() const override { return std::vector<std::size_t>(length_, size_); }
    std::shared_ptr<const Content> flatten() const override { return content_; }

    std::shared_ptr<const Content> to_packed() const override {
        return std::make_shared<RegularContentArray>(content_->to_packed(), size_, length_);
    }

    std::shared_ptr<const Content> local_index() const override {
        std::vector<std::int64_t> values;
        values.reserve(content_->length());
        for (std::size_t row = 0; row < length_; ++row) {
            for (std::size_t i = 0; i < size_; ++i) values.push_back(static_cast<std::int64_t>(i));
        }
        return std::make_shared<RegularContentArray>(
            std::make_shared<NumpyArray<std::int64_t>>(std::move(values)), size_, length_);
    }

    std::size_t size() const noexcept { return size_; }
    const Content& content() const noexcept { return *content_; }

    Form to_buffers(detail::BufferBuilder& builder) const override {
        auto key = builder.next_key();
        auto content_form = content_->to_buffers(builder);
        return Form{
            .kind = FormKind::regular,
            .key = std::move(key),
            .contents = {std::move(content_form)},
            .length = length_,
            .size = size_,
        };
    }

private:
    ListOffsetContentArray as_list_offset() const {
        std::vector<std::size_t> offsets;
        offsets.reserve(length_ + 1);
        for (std::size_t row = 0; row <= length_; ++row) offsets.push_back(row * size_);
        return ListOffsetContentArray(content_, std::move(offsets));
    }

    std::string validate() const {
        if (!content_) return "ak::RegularContentArray content must not be null";
        const auto error = content_->validity_error();
        if (!error.empty()) return error;
        if (size_ == 0) return content_->length() == 0 ? std::string{} :
                                                           "ak::RegularContentArray size zero requires empty content";
        if (length_ > std::numeric_limits<std::size_t>::max() / size_ || length_ * size_ != content_->length()) {
            return "ak::RegularContentArray content length must equal length times regular size";
        }
        return {};
    }

    std::shared_ptr<const Content> content_;
    std::size_t size_;
    std::size_t length_;
};

}  // namespace ak

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

namespace ak::kernel {

enum class BinaryOperation {
    add,
    subtract,
    multiply,
    divide,
    equal,
    not_equal,
    less,
    less_equal,
    greater,
    greater_equal,
    logical_and,
    logical_or,
};

enum class Shape { flat, list_offset, regular };

struct NumericLayout {
    Shape shape{Shape::flat};
    std::vector<long double> values;
    std::vector<std::size_t> offsets;
    std::size_t regular_size{0};
    std::size_t regular_length{0};
    bool real{false};
};

template <typename T>
inline std::optional<NumericLayout> numeric_layout_as(const Content& layout) {
    if (const auto* flat = dynamic_cast<const NumpyArray<T>*>(&layout)) {
        NumericLayout result;
        result.values.reserve(flat->length());
        for (const auto value : flat->values_vector()) result.values.push_back(static_cast<long double>(value));
        result.real = std::is_floating_point_v<T>;
        return result;
    }
    if (const auto* list = dynamic_cast<const ListOffsetArray<T>*>(&layout)) {
        NumericLayout result;
        result.shape = Shape::list_offset;
        result.offsets.assign(list->offsets().begin(), list->offsets().end());
        result.values.reserve(list->content().length());
        for (const auto value : list->content().values_vector()) result.values.push_back(static_cast<long double>(value));
        result.real = std::is_floating_point_v<T>;
        return result;
    }
    if (const auto* regular = dynamic_cast<const RegularArray<T>*>(&layout)) {
        NumericLayout result;
        result.shape = Shape::regular;
        result.regular_size = regular->size();
        result.regular_length = regular->length();
        result.values.reserve(regular->content().length());
        for (const auto value : regular->content().values_vector()) result.values.push_back(static_cast<long double>(value));
        result.real = std::is_floating_point_v<T>;
        return result;
    }
    return std::nullopt;
}

inline std::optional<NumericLayout> numeric_layout(const Content& layout) {
    if (auto result = numeric_layout_as<bool>(layout)) return result;
    if (auto result = numeric_layout_as<char>(layout)) return result;
    if (auto result = numeric_layout_as<signed char>(layout)) return result;
    if (auto result = numeric_layout_as<unsigned char>(layout)) return result;
    if (auto result = numeric_layout_as<short>(layout)) return result;
    if (auto result = numeric_layout_as<unsigned short>(layout)) return result;
    if (auto result = numeric_layout_as<int>(layout)) return result;
    if (auto result = numeric_layout_as<unsigned int>(layout)) return result;
    if (auto result = numeric_layout_as<long>(layout)) return result;
    if (auto result = numeric_layout_as<unsigned long>(layout)) return result;
    if (auto result = numeric_layout_as<long long>(layout)) return result;
    if (auto result = numeric_layout_as<unsigned long long>(layout)) return result;
    if (auto result = numeric_layout_as<std::int64_t>(layout)) return result;
    if (auto result = numeric_layout_as<std::uint64_t>(layout)) return result;
    if (auto result = numeric_layout_as<float>(layout)) return result;
    if (auto result = numeric_layout_as<double>(layout)) return result;
    return std::nullopt;
}

inline bool same_shape(const NumericLayout& left, const NumericLayout& right) {
    return left.shape == right.shape && left.values.size() == right.values.size() &&
           left.offsets == right.offsets && left.regular_size == right.regular_size &&
           left.regular_length == right.regular_length;
}

inline bool boolean_result(BinaryOperation operation) {
    return operation == BinaryOperation::equal || operation == BinaryOperation::not_equal ||
           operation == BinaryOperation::less || operation == BinaryOperation::less_equal ||
           operation == BinaryOperation::greater || operation == BinaryOperation::greater_equal ||
           operation == BinaryOperation::logical_and || operation == BinaryOperation::logical_or;
}

inline long double apply(long double left, long double right, BinaryOperation operation) {
    switch (operation) {
    case BinaryOperation::add: return left + right;
    case BinaryOperation::subtract: return left - right;
    case BinaryOperation::multiply: return left * right;
    case BinaryOperation::divide: return left / right;
    case BinaryOperation::equal: return left == right;
    case BinaryOperation::not_equal: return left != right;
    case BinaryOperation::less: return left < right;
    case BinaryOperation::less_equal: return left <= right;
    case BinaryOperation::greater: return left > right;
    case BinaryOperation::greater_equal: return left >= right;
    case BinaryOperation::logical_and: return left != 0.0 && right != 0.0;
    case BinaryOperation::logical_or: return left != 0.0 || right != 0.0;
    }
    throw std::invalid_argument("unknown binary kernel operation");
}

template <typename T>
inline std::shared_ptr<const Content> build_layout(const NumericLayout& shape, std::vector<T> values) {
    if (shape.shape == Shape::flat) return std::make_shared<NumpyArray<T>>(std::move(values));
    if (shape.shape == Shape::list_offset) {
        return std::make_shared<ListOffsetArray<T>>(std::move(values), shape.offsets);
    }
    return std::make_shared<RegularArray<T>>(std::move(values), shape.regular_size, shape.regular_length);
}

inline std::shared_ptr<const Content> apply_numeric(const NumericLayout& shape,
                                                    const std::vector<long double>& right,
                                                    BinaryOperation operation,
                                                    bool real_result) {
    if (boolean_result(operation)) {
        std::vector<bool> values;
        values.reserve(shape.values.size());
        for (std::size_t i = 0; i < shape.values.size(); ++i) values.push_back(apply(shape.values[i], right[i], operation) != 0.0);
        return build_layout(shape, std::move(values));
    }
    if (real_result || operation == BinaryOperation::divide) {
        std::vector<double> values;
        values.reserve(shape.values.size());
        for (std::size_t i = 0; i < shape.values.size(); ++i) values.push_back(apply(shape.values[i], right[i], operation));
        return build_layout(shape, std::move(values));
    }
    std::vector<std::int64_t> values;
    values.reserve(shape.values.size());
    for (std::size_t i = 0; i < shape.values.size(); ++i) values.push_back(static_cast<std::int64_t>(apply(shape.values[i], right[i], operation)));
    return build_layout(shape, std::move(values));
}

inline std::shared_ptr<const Content> binary(const Content& left,
                                             const Content& right,
                                             BinaryOperation operation) {
    const auto left_values = numeric_layout(left);
    const auto right_values = numeric_layout(right);
    if (!left_values || !right_values || !same_shape(*left_values, *right_values)) return nullptr;
    return apply_numeric(*left_values, right_values->values, operation, left_values->real || right_values->real);
}

inline std::optional<std::pair<long double, bool>> numeric_scalar(const Value& value) {
    if (const auto* boolean = std::get_if<bool>(&value.storage())) return std::pair<long double, bool>{*boolean ? 1.0L : 0.0L, false};
    if (const auto* integer = std::get_if<std::int64_t>(&value.storage())) return std::pair<long double, bool>{static_cast<long double>(*integer), false};
    if (const auto* real = std::get_if<double>(&value.storage())) return std::pair<long double, bool>{*real, true};
    return std::nullopt;
}

inline std::shared_ptr<const Content> binary(const Content& layout,
                                             const Value& scalar,
                                             BinaryOperation operation,
                                             bool scalar_on_left = false) {
    const auto values = numeric_layout(layout);
    const auto scalar_value = numeric_scalar(scalar);
    if (!values || !scalar_value) return nullptr;
    std::vector<long double> scalar_values(values->values.size(), scalar_value->first);
    if (!scalar_on_left) return apply_numeric(*values, scalar_values, operation, values->real || scalar_value->second);
    NumericLayout scalar_shape = *values;
    scalar_shape.values = std::move(scalar_values);
    return apply_numeric(scalar_shape, values->values, operation, values->real || scalar_value->second);
}

inline std::shared_ptr<const Content> logical_not(const Content& layout) {
    const auto values = numeric_layout(layout);
    if (!values) return nullptr;
    std::vector<bool> result;
    result.reserve(values->values.size());
    for (const auto value : values->values) result.push_back(value == 0.0);
    return build_layout(*values, std::move(result));
}

template <typename T>
inline std::shared_ptr<const Content> concatenate_typed(const std::vector<Array>& arrays) {
    bool all_flat = true;
    bool all_list = true;
    std::vector<T> values;
    std::vector<std::size_t> offsets{0};
    for (const auto& array : arrays) {
        if (const auto* flat = dynamic_cast<const NumpyArray<T>*>(&array.layout())) {
            values.insert(values.end(), flat->values_vector().begin(), flat->values_vector().end());
            all_list = false;
        } else if (const auto* list = dynamic_cast<const ListOffsetArray<T>*>(&array.layout())) {
            values.insert(values.end(), list->content().values_vector().begin(), list->content().values_vector().end());
            for (std::size_t i = 1; i < list->offsets().size(); ++i) offsets.push_back(offsets.back() + list->offsets()[i] - list->offsets()[i - 1]);
            all_flat = false;
        } else {
            return nullptr;
        }
    }
    if (all_flat) return std::make_shared<NumpyArray<T>>(std::move(values));
    if (all_list) return std::make_shared<ListOffsetArray<T>>(std::move(values), std::move(offsets));
    return nullptr;
}

inline std::shared_ptr<const Content> concatenate_axis0(const std::vector<Array>& arrays) {
    if (arrays.empty()) return nullptr;
    if (arrays.size() == 1) return arrays.front().layout_ptr();
    if (auto result = concatenate_typed<bool>(arrays)) return result;
    if (auto result = concatenate_typed<char>(arrays)) return result;
    if (auto result = concatenate_typed<signed char>(arrays)) return result;
    if (auto result = concatenate_typed<unsigned char>(arrays)) return result;
    if (auto result = concatenate_typed<short>(arrays)) return result;
    if (auto result = concatenate_typed<unsigned short>(arrays)) return result;
    if (auto result = concatenate_typed<int>(arrays)) return result;
    if (auto result = concatenate_typed<unsigned int>(arrays)) return result;
    if (auto result = concatenate_typed<long>(arrays)) return result;
    if (auto result = concatenate_typed<unsigned long>(arrays)) return result;
    if (auto result = concatenate_typed<long long>(arrays)) return result;
    if (auto result = concatenate_typed<unsigned long long>(arrays)) return result;
    if (auto result = concatenate_typed<std::int64_t>(arrays)) return result;
    if (auto result = concatenate_typed<std::uint64_t>(arrays)) return result;
    if (auto result = concatenate_typed<float>(arrays)) return result;
    if (auto result = concatenate_typed<double>(arrays)) return result;

    std::vector<std::uint8_t> tags;
    std::vector<std::ptrdiff_t> index;
    std::vector<std::shared_ptr<const Content>> contents;
    contents.reserve(arrays.size());
    for (std::size_t tag = 0; tag < arrays.size(); ++tag) {
        if (tag > 255) throw std::invalid_argument("axis-0 concatenation supports at most 256 layouts");
        contents.push_back(arrays[tag].layout_ptr());
        for (std::size_t i = 0; i < arrays[tag].length(); ++i) {
            tags.push_back(static_cast<std::uint8_t>(tag));
            index.push_back(static_cast<std::ptrdiff_t>(i));
        }
    }
    return std::make_shared<UnionArray>(std::move(tags), std::move(index), std::move(contents));
}

inline std::shared_ptr<const Content> carry(const Content& layout, const std::vector<std::ptrdiff_t>& indices) {
    return layout.take_rows(indices);
}

inline std::shared_ptr<const Content> mask(const Content& layout, const Content& mask) {
    return layout.mask_as_array(mask);
}

}  // namespace ak::kernel

namespace ak {

namespace index {

inline Item array(const Array& array) {
    return ArrayIndex{array.layout_ptr()};
}

}  // namespace index

namespace detail {

enum class ValueTag {
    none,
    boolean,
    integer,
    real,
    string,
    list,
    record,
};

inline ValueTag value_tag(const Value& value) {
    return std::visit(
        [](const auto& item) {
            using Item = std::decay_t<decltype(item)>;
            if constexpr (std::same_as<Item, std::monostate>) {
                return ValueTag::none;
            } else if constexpr (std::same_as<Item, bool>) {
                return ValueTag::boolean;
            } else if constexpr (std::same_as<Item, std::int64_t>) {
                return ValueTag::integer;
            } else if constexpr (std::same_as<Item, double>) {
                return ValueTag::real;
            } else if constexpr (std::same_as<Item, std::string>) {
                return ValueTag::string;
            } else if constexpr (std::same_as<Item, Value::list_type>) {
                return ValueTag::list;
            } else {
                return ValueTag::record;
            }
        },
        value.storage());
}

inline bool is_scalar_or_none(const Value& value) {
    const auto tag = value_tag(value);
    return tag != ValueTag::list;
}

inline ValueTag merged_scalar_tag(const Value::list_type& values) {
    std::optional<ValueTag> tag;
    for (const auto& value : values) {
        const auto current = value_tag(value);
        if (current == ValueTag::none) {
            continue;
        }
        if (current == ValueTag::list) {
            throw std::invalid_argument("expected scalar values while building primitive content");
        }
        if (!tag) {
            tag = current;
            continue;
        }
        if ((*tag == ValueTag::integer && current == ValueTag::real) ||
            (*tag == ValueTag::real && current == ValueTag::integer)) {
            tag = ValueTag::real;
            continue;
        }
        if (*tag != current) {
            throw std::invalid_argument("mixed scalar values require union reconstruction");
        }
    }
    return tag.value_or(ValueTag::none);
}

template <typename T>
T value_as(const Value& value) {
    const auto& storage = value.storage();
    if constexpr (std::same_as<T, bool>) {
        return std::get<bool>(storage);
    } else if constexpr (std::same_as<T, std::int64_t>) {
        return std::get<std::int64_t>(storage);
    } else if constexpr (std::same_as<T, double>) {
        if (const auto* integer = std::get_if<std::int64_t>(&storage)) {
            return static_cast<double>(*integer);
        }
        return std::get<double>(storage);
    } else {
        return std::get<std::string>(storage);
    }
}

template <typename T>
std::shared_ptr<const Content> primitive_layout_from_values(const Value::list_type& values, bool has_none) {
    std::vector<T> content_values;
    std::vector<std::ptrdiff_t> index;
    content_values.reserve(values.size());
    index.reserve(values.size());

    for (const auto& value : values) {
        if (value.is_none()) {
            index.push_back(-1);
            continue;
        }
        index.push_back(static_cast<std::ptrdiff_t>(content_values.size()));
        content_values.push_back(value_as<T>(value));
    }

    std::shared_ptr<const Content> content;
    if constexpr (std::same_as<T, std::string>) {
        content = std::make_shared<StringArray>(content_values);
    } else {
        content = std::make_shared<NumpyArray<T>>(std::move(content_values));
    }
    if (has_none) {
        return std::make_shared<IndexedOptionArray>(std::move(index), content);
    }
    return content;
}

inline std::shared_ptr<const Content> layout_from_list(const Value::list_type& values);

inline std::string union_group_key(const Value& value) {
    switch (value_tag(value)) {
    case ValueTag::boolean:
        return "boolean";
    case ValueTag::integer:
    case ValueTag::real:
        return "number";
    case ValueTag::string:
        return "string";
    case ValueTag::list:
        return "list";
    case ValueTag::record: {
        const auto& record = value.as_record();
        std::string key = record.is_tuple ? "tuple" : "record";
        for (const auto& field : record.fields) {
            key += ':' + std::to_string(field.size()) + ':' + field;
        }
        return key;
    }
    case ValueTag::none:
        break;
    }
    throw std::invalid_argument("missing values do not have a union content group");
}

inline std::shared_ptr<const Content> union_layout_from_values(const Value::list_type& values) {
    struct Group {
        std::string key;
        Value::list_type values;
    };

    std::vector<Group> groups;
    std::vector<std::uint8_t> tags;
    std::vector<std::ptrdiff_t> index;
    std::vector<std::ptrdiff_t> option_index;
    bool has_none = false;
    tags.reserve(values.size());
    index.reserve(values.size());
    option_index.reserve(values.size());

    for (const auto& value : values) {
        if (value.is_none()) {
            has_none = true;
            option_index.push_back(-1);
            continue;
        }

        const auto key = union_group_key(value);
        auto group = std::find_if(groups.begin(), groups.end(), [&key](const auto& candidate) {
            return candidate.key == key;
        });
        if (group == groups.end()) {
            groups.push_back(Group{key, {}});
            group = std::prev(groups.end());
        }
        const auto tag = static_cast<std::size_t>(std::distance(groups.begin(), group));
        if (tag > std::numeric_limits<std::uint8_t>::max()) {
            throw std::invalid_argument("ak::UnionArray supports at most 256 contents");
        }
        tags.push_back(static_cast<std::uint8_t>(tag));
        index.push_back(static_cast<std::ptrdiff_t>(group->values.size()));
        group->values.push_back(value);
        option_index.push_back(static_cast<std::ptrdiff_t>(tags.size() - 1));
    }

    if (groups.size() < 2) {
        throw std::invalid_argument("union construction requires at least two content groups");
    }

    std::vector<std::shared_ptr<const Content>> contents;
    contents.reserve(groups.size());
    for (const auto& group : groups) {
        contents.push_back(layout_from_list(group.values));
    }

    std::shared_ptr<const Content> result =
        std::make_shared<UnionArray>(std::move(tags), std::move(index), std::move(contents));
    if (has_none) {
        result = std::make_shared<IndexedOptionArray>(std::move(option_index), std::move(result));
    }
    return result;
}

inline void validate_record_shape(const Value::record_type& expected, const Value::record_type& actual) {
    if (actual.is_tuple != expected.is_tuple || actual.fields != expected.fields) {
        throw std::invalid_argument("record values must have matching fields and tuple state");
    }
    if (actual.values.size() != expected.values.size()) {
        throw std::invalid_argument("record values must have matching field counts");
    }
}

inline std::shared_ptr<const Content> layout_from_record_values(const Value::list_type& values) {
    bool has_none = false;
    Value::list_type present_records;
    std::vector<std::ptrdiff_t> record_index;
    present_records.reserve(values.size());
    record_index.reserve(values.size());

    for (const auto& value : values) {
        if (value.is_none()) {
            has_none = true;
            record_index.push_back(-1);
            continue;
        }
        if (value_tag(value) != ValueTag::record) {
            throw std::invalid_argument("record content requires matching record values");
        }
        record_index.push_back(static_cast<std::ptrdiff_t>(present_records.size()));
        present_records.push_back(value);
    }

    if (present_records.empty()) {
        return std::make_shared<IndexedOptionArray>(
            std::vector<std::ptrdiff_t>(values.size(), -1), std::make_shared<EmptyArray>());
    }

    const auto& first = present_records.front().as_record();
    std::vector<Value::list_type> field_values(first.values.size());
    for (const auto& value : present_records) {
        const auto& record = value.as_record();
        validate_record_shape(first, record);
        for (std::size_t i = 0; i < field_values.size(); ++i) {
            field_values[i].push_back(record.values[i]);
        }
    }

    std::vector<std::shared_ptr<const Content>> contents;
    contents.reserve(field_values.size());
    for (auto& field : field_values) {
        contents.push_back(layout_from_list(field));
    }

    std::shared_ptr<const Content> records =
        std::make_shared<RecordArray>(first.fields, std::move(contents), first.is_tuple, std::string{}, present_records.size());
    if (has_none) {
        return std::make_shared<IndexedOptionArray>(std::move(record_index), records);
    }
    return records;
}

inline std::shared_ptr<const Content> scalar_layout_from_values(const Value::list_type& values) {
    if (values.empty()) {
        return std::make_shared<EmptyArray>();
    }
    bool has_none = false;
    for (const auto& value : values) {
        has_none = has_none || value.is_none();
    }

    switch (merged_scalar_tag(values)) {
    case ValueTag::none:
        return std::make_shared<IndexedOptionArray>(
            std::vector<std::ptrdiff_t>(values.size(), -1), std::make_shared<EmptyArray>());
    case ValueTag::boolean:
        return primitive_layout_from_values<bool>(values, has_none);
    case ValueTag::integer:
        return primitive_layout_from_values<std::int64_t>(values, has_none);
    case ValueTag::real:
        return primitive_layout_from_values<double>(values, has_none);
    case ValueTag::string:
        return primitive_layout_from_values<std::string>(values, has_none);
    case ValueTag::list:
    case ValueTag::record:
        break;
    }
    throw std::invalid_argument("unsupported scalar layout");
}

inline std::shared_ptr<const Content> layout_from_list_rows(const Value::list_type& values) {
    bool has_missing_rows = false;
    Value::list_type present_rows;
    std::vector<std::ptrdiff_t> row_index;
    present_rows.reserve(values.size());
    row_index.reserve(values.size());

    for (const auto& row : values) {
        if (row.is_none()) {
            has_missing_rows = true;
            row_index.push_back(-1);
            continue;
        }
        if (value_tag(row) != ValueTag::list) {
            throw std::invalid_argument("list content requires list values");
        }
        row_index.push_back(static_cast<std::ptrdiff_t>(present_rows.size()));
        present_rows.push_back(row);
    }

    std::vector<std::size_t> offsets;
    Value::list_type flat_values;
    offsets.reserve(present_rows.size() + 1);
    offsets.push_back(0);
    for (const auto& row : present_rows) {
        for (const auto& item : row.as_list()) {
            flat_values.push_back(item);
        }
        offsets.push_back(flat_values.size());
    }

    auto content = layout_from_list(flat_values);
    std::shared_ptr<const Content> rows;
    if (content->kind() == LayoutKind::numpy) {
        switch (merged_scalar_tag(flat_values)) {
        case ValueTag::boolean: {
            std::vector<bool> raw;
            raw.reserve(flat_values.size());
            for (const auto& value : flat_values) {
                raw.push_back(value_as<bool>(value));
            }
            rows = std::make_shared<ListOffsetArray<bool>>(std::move(raw), std::move(offsets));
            break;
        }
        case ValueTag::integer: {
            std::vector<std::int64_t> raw;
            raw.reserve(flat_values.size());
            for (const auto& value : flat_values) {
                raw.push_back(value_as<std::int64_t>(value));
            }
            rows = std::make_shared<ListOffsetArray<std::int64_t>>(std::move(raw), std::move(offsets));
            break;
        }
        case ValueTag::real: {
            std::vector<double> raw;
            raw.reserve(flat_values.size());
            for (const auto& value : flat_values) {
                raw.push_back(value_as<double>(value));
            }
            rows = std::make_shared<ListOffsetArray<double>>(std::move(raw), std::move(offsets));
            break;
        }
        case ValueTag::string: {
            std::vector<std::string> raw;
            raw.reserve(flat_values.size());
            for (const auto& value : flat_values) {
                raw.push_back(value_as<std::string>(value));
            }
            rows = std::make_shared<ListOffsetArray<std::string>>(std::move(raw), std::move(offsets));
            break;
        }
        case ValueTag::none:
        case ValueTag::list:
        case ValueTag::record:
            rows = std::make_shared<ListOffsetContentArray>(content, std::move(offsets));
            break;
        }
    } else {
        rows = std::make_shared<ListOffsetContentArray>(content, std::move(offsets));
    }

    if (has_missing_rows) {
        return std::make_shared<IndexedOptionArray>(std::move(row_index), rows);
    }
    return rows;
}

inline std::shared_ptr<const Content> layout_from_list(const Value::list_type& values) {
    bool has_list = false;
    bool has_scalar = false;
    bool has_record = false;
    for (const auto& value : values) {
        if (value.is_none()) {
            continue;
        }
        if (value_tag(value) == ValueTag::list) {
            has_list = true;
        } else if (value_tag(value) == ValueTag::record) {
            has_record = true;
        } else {
            has_scalar = true;
        }
    }
    const auto categories = static_cast<int>(has_list) + static_cast<int>(has_scalar) + static_cast<int>(has_record);
    if (categories > 1) {
        return union_layout_from_values(values);
    }

    if (categories == 1) {
        std::vector<std::string> groups;
        for (const auto& value : values) {
            if (value.is_none()) {
                continue;
            }
            const auto key = union_group_key(value);
            if (std::find(groups.begin(), groups.end(), key) == groups.end()) {
                groups.push_back(key);
            }
        }
        if (groups.size() > 1) {
            return union_layout_from_values(values);
        }
    }
    if (has_list) {
        return layout_from_list_rows(values);
    }
    if (has_record) {
        return layout_from_record_values(values);
    }
    return scalar_layout_from_values(values);
}

inline Array array_from_list(Value::list_type values) {
    return Array(layout_from_list(values));
}

inline Value concatenate_values(const std::vector<Value>& values, int axis, int depth = 0) {
    if (values.empty()) {
        return Value::list_type{};
    }
    if (depth == axis) {
        Value::list_type result;
        for (const auto& value : values) {
            if (value_tag(value) != ValueTag::list) {
                throw std::invalid_argument("ak::concatenate axis requires list values");
            }
            result.insert(result.end(), value.as_list().begin(), value.as_list().end());
        }
        return result;
    }

    for (const auto& value : values) {
        if (value_tag(value) != ValueTag::list) {
            throw std::invalid_argument("ak::concatenate axis is deeper than an input layout");
        }
    }
    const auto length = values.front().as_list().size();
    for (const auto& value : values) {
        if (value.as_list().size() != length) {
            throw std::invalid_argument("ak::concatenate requires matching lengths before the selected axis");
        }
    }

    Value::list_type result;
    result.reserve(length);
    for (std::size_t i = 0; i < length; ++i) {
        std::vector<Value> children;
        children.reserve(values.size());
        for (const auto& value : values) children.push_back(value.as_list()[i]);
        result.push_back(concatenate_values(children, axis, depth + 1));
    }
    return result;
}

inline Value num_at_axis(const Value& value, int axis, int depth = 0) {
    if (value.is_none()) return Value(nullptr);
    if (value_tag(value) != ValueTag::list) {
        throw std::invalid_argument("ak::num axis is deeper than an input value");
    }
    if (depth == axis) return static_cast<std::int64_t>(value.as_list().size());
    Value::list_type result;
    result.reserve(value.as_list().size());
    for (const auto& item : value.as_list()) result.push_back(num_at_axis(item, axis, depth + 1));
    return result;
}

inline Value flatten_at_axis(const Value& value, int axis, int depth = 0) {
    if (value.is_none()) return Value(nullptr);
    if (value_tag(value) != ValueTag::list) {
        throw std::invalid_argument("ak::flatten axis is deeper than an input value");
    }
    if (depth + 1 == axis) {
        Value::list_type result;
        for (const auto& item : value.as_list()) {
            if (item.is_none()) continue;
            if (value_tag(item) != ValueTag::list) {
                throw std::invalid_argument("ak::flatten requires list values at the selected axis");
            }
            result.insert(result.end(), item.as_list().begin(), item.as_list().end());
        }
        return result;
    }
    Value::list_type result;
    result.reserve(value.as_list().size());
    for (const auto& item : value.as_list()) result.push_back(flatten_at_axis(item, axis, depth + 1));
    return result;
}

inline void collect_ravel_values(const Value& value, Value::list_type& result) {
    if (value_tag(value) != ValueTag::list) {
        result.push_back(value);
        return;
    }
    for (const auto& item : value.as_list()) collect_ravel_values(item, result);
}

inline Value local_index_at_axis(const Value& value, int axis, int depth = 0) {
    if (value.is_none()) return Value(nullptr);
    if (value_tag(value) != ValueTag::list) {
        throw std::invalid_argument("ak::local_index axis is deeper than an input value");
    }
    if (depth == axis) {
        Value::list_type result;
        result.reserve(value.as_list().size());
        for (std::size_t i = 0; i < value.as_list().size(); ++i) {
            result.emplace_back(static_cast<std::int64_t>(i));
        }
        return result;
    }
    Value::list_type result;
    result.reserve(value.as_list().size());
    for (const auto& item : value.as_list()) result.push_back(local_index_at_axis(item, axis, depth + 1));
    return result;
}

inline Value::list_type require_top_list(const Array& array) {
    const auto value = array.to_list();
    if (value_tag(value) != ValueTag::list) {
        throw std::invalid_argument("operation requires an array list value");
    }
    return value.as_list();
}

inline Value is_none_value(const Value& value, int axis, int depth) {
    if (axis == depth) {
        return value.is_none();
    }
    if (value.is_none()) {
        return Value(nullptr);
    }
    if (value_tag(value) != ValueTag::list) {
        throw std::invalid_argument("ak::is_none axis is deeper than this layout");
    }
    Value::list_type result;
    for (const auto& item : value.as_list()) {
        result.push_back(is_none_value(item, axis, depth + 1));
    }
    return result;
}

inline bool should_drop_at_axis(const Value& value, std::optional<int> axis, int depth) {
    if (!value.is_none()) {
        return false;
    }
    return !axis || *axis == depth;
}

inline Value drop_none_value(const Value& value, std::optional<int> axis, int depth) {
    if (value.is_none()) {
        return Value(nullptr);
    }
    if (value_tag(value) != ValueTag::list) {
        return value;
    }
    Value::list_type result;
    for (const auto& item : value.as_list()) {
        if (!should_drop_at_axis(item, axis, depth + 1)) {
            result.push_back(drop_none_value(item, axis, depth + 1));
        }
    }
    return result;
}

inline Value fill_none_value(const Value& value, const Value& fill_value) {
    if (value.is_none()) {
        return fill_value;
    }
    if (value_tag(value) != ValueTag::list) {
        return value;
    }
    Value::list_type result;
    for (const auto& item : value.as_list()) {
        result.push_back(fill_none_value(item, fill_value));
    }
    return result;
}

inline Value pad_none_value(const Value& value, std::size_t target, int axis, bool clip, int depth) {
    if (value.is_none()) {
        return Value(nullptr);
    }
    if (value_tag(value) != ValueTag::list) {
        throw std::invalid_argument("ak::pad_none axis is deeper than this layout");
    }

    Value::list_type result;
    if (axis == depth) {
        result = value.as_list();
        if (clip && result.size() > target) {
            result.resize(target);
        }
        while (result.size() < target) {
            result.emplace_back(nullptr);
        }
        return result;
    }

    for (const auto& item : value.as_list()) {
        result.push_back(pad_none_value(item, target, axis, clip, depth + 1));
    }
    return result;
}

inline Value nan_to_none_value(const Value& value) {
    if (const auto* real = std::get_if<double>(&value.storage())) {
        if (std::isnan(*real)) {
            return Value(nullptr);
        }
        return value;
    }
    if (value_tag(value) != ValueTag::list) {
        return value;
    }
    Value::list_type result;
    for (const auto& item : value.as_list()) {
        result.push_back(nan_to_none_value(item));
    }
    return result;
}

inline Value nan_to_num_value(const Value& value, double nan, double posinf, double neginf) {
    if (const auto* real = std::get_if<double>(&value.storage())) {
        if (std::isnan(*real)) {
            return nan;
        }
        if (*real == std::numeric_limits<double>::infinity()) {
            return posinf;
        }
        if (*real == -std::numeric_limits<double>::infinity()) {
            return neginf;
        }
        return value;
    }
    if (value_tag(value) != ValueTag::list) {
        return value;
    }
    Value::list_type result;
    for (const auto& item : value.as_list()) {
        result.push_back(nan_to_num_value(item, nan, posinf, neginf));
    }
    return result;
}

inline Value mask_value(const Value& value, const Value& mask, bool valid_when) {
    if (value_tag(mask) == ValueTag::boolean) {
        const auto keep = std::get<bool>(mask.storage()) == valid_when;
        return keep ? value : Value(nullptr);
    }
    if (value_tag(value) != ValueTag::list || value_tag(mask) != ValueTag::list) {
        throw std::invalid_argument("ak::mask requires matching boolean mask structure");
    }
    const auto& values = value.as_list();
    const auto& masks = mask.as_list();
    if (values.size() != masks.size()) {
        throw std::invalid_argument("ak::mask requires matching mask lengths");
    }
    Value::list_type result;
    result.reserve(values.size());
    for (std::size_t i = 0; i < values.size(); ++i) {
        result.push_back(mask_value(values[i], masks[i], valid_when));
    }
    return result;
}

inline Value firsts_value(const Value& value, int axis, int depth) {
    if (value.is_none()) {
        return Value(nullptr);
    }
    if (value_tag(value) != ValueTag::list) {
        throw std::invalid_argument("ak::firsts axis is deeper than this layout");
    }
    if (axis == depth + 1) {
        Value::list_type result;
        for (const auto& row : value.as_list()) {
            if (row.is_none()) {
                result.emplace_back(nullptr);
                continue;
            }
            if (value_tag(row) != ValueTag::list) {
                throw std::invalid_argument("ak::firsts requires list values at the selected axis");
            }
            const auto& row_values = row.as_list();
            result.push_back(row_values.empty() ? Value(nullptr) : row_values.front());
        }
        return result;
    }
    Value::list_type result;
    for (const auto& item : value.as_list()) {
        result.push_back(firsts_value(item, axis, depth + 1));
    }
    return result;
}

inline Value singletons_value(const Value& value) {
    Value::list_type result;
    for (const auto& item : value.as_list()) {
        if (item.is_none()) {
            result.emplace_back(Value::list_type{});
        } else {
            result.emplace_back(Value::list_type{item});
        }
    }
    return result;
}

enum class ReducerKind {
    count,
    count_nonzero,
    sum,
    prod,
    any,
    all,
    min,
    max,
    argmin,
    argmax,
    mean,
    var,
    stddev,
    moment,
    ptp,
};

enum class BinaryOpKind {
    add,
    subtract,
    multiply,
    divide,
    equal,
    not_equal,
    less,
    less_equal,
    greater,
    greater_equal,
    logical_and,
    logical_or,
};

inline kernel::BinaryOperation kernel_operation(BinaryOpKind kind) {
    switch (kind) {
    case BinaryOpKind::add: return kernel::BinaryOperation::add;
    case BinaryOpKind::subtract: return kernel::BinaryOperation::subtract;
    case BinaryOpKind::multiply: return kernel::BinaryOperation::multiply;
    case BinaryOpKind::divide: return kernel::BinaryOperation::divide;
    case BinaryOpKind::equal: return kernel::BinaryOperation::equal;
    case BinaryOpKind::not_equal: return kernel::BinaryOperation::not_equal;
    case BinaryOpKind::less: return kernel::BinaryOperation::less;
    case BinaryOpKind::less_equal: return kernel::BinaryOperation::less_equal;
    case BinaryOpKind::greater: return kernel::BinaryOperation::greater;
    case BinaryOpKind::greater_equal: return kernel::BinaryOperation::greater_equal;
    case BinaryOpKind::logical_and: return kernel::BinaryOperation::logical_and;
    case BinaryOpKind::logical_or: return kernel::BinaryOperation::logical_or;
    }
    throw std::invalid_argument("unknown binary operation");
}

inline bool is_numeric_tag(ValueTag tag) noexcept {
    return tag == ValueTag::boolean || tag == ValueTag::integer || tag == ValueTag::real;
}

inline int array_depth(const Value& value) {
    if (value.is_none() || value_tag(value) != ValueTag::list) {
        return 0;
    }
    int depth = 1;
    for (const auto& item : value.as_list()) {
        depth = std::max(depth, 1 + array_depth(item));
    }
    return depth;
}

inline int normalize_axis(std::optional<int> axis, int depth) {
    if (!axis) {
        return -1;
    }
    int normalized = *axis;
    if (normalized < 0) {
        normalized += depth;
    }
    if (normalized < 0 || normalized >= depth) {
        throw std::invalid_argument("reducer axis is outside the implemented layout depth");
    }
    return normalized;
}

inline bool is_nan_value(const Value& value) {
    if (const auto* real = std::get_if<double>(&value.storage())) {
        return std::isnan(*real);
    }
    return false;
}

inline bool is_missing_for_reducer(const Value& value, bool skip_nan) {
    return value.is_none() || (skip_nan && is_nan_value(value));
}

inline bool contains_real_scalar(const Value& value) {
    if (value_tag(value) == ValueTag::real) {
        return true;
    }
    if (value_tag(value) != ValueTag::list) {
        return false;
    }
    for (const auto& item : value.as_list()) {
        if (contains_real_scalar(item)) {
            return true;
        }
    }
    return false;
}

inline double numeric_as_double(const Value& value) {
    const auto& storage = value.storage();
    if (const auto* boolean = std::get_if<bool>(&storage)) {
        return *boolean ? 1.0 : 0.0;
    }
    if (const auto* integer = std::get_if<std::int64_t>(&storage)) {
        return static_cast<double>(*integer);
    }
    if (const auto* real = std::get_if<double>(&storage)) {
        return *real;
    }
    throw std::invalid_argument("numeric reducer received a non-numeric value");
}

inline bool value_truthy(const Value& value) {
    const auto tag = value_tag(value);
    if (tag == ValueTag::boolean) {
        return std::get<bool>(value.storage());
    }
    if (tag == ValueTag::integer) {
        return std::get<std::int64_t>(value.storage()) != 0;
    }
    if (tag == ValueTag::real) {
        return std::get<double>(value.storage()) != 0.0;
    }
    throw std::invalid_argument("truth-value reducer received a non-numeric value");
}

inline bool prefer_real_result(const Value& value) {
    return value_tag(value) == ValueTag::real;
}

inline Value numeric_result(double value, bool real_result) {
    if (real_result) {
        return value;
    }
    return static_cast<std::int64_t>(value);
}

struct ReduceSettings {
    ReducerKind kind;
    bool mask_identity{false};
    std::optional<Value> initial;
    bool skip_nan{false};
    double ddof{0.0};
    int moment_order{2};
};

inline void collect_scalars(const Value& value, Value::list_type& scalars, bool skip_nan) {
    if (is_missing_for_reducer(value, skip_nan)) {
        return;
    }
    if (value_tag(value) == ValueTag::list) {
        for (const auto& item : value.as_list()) {
            collect_scalars(item, scalars, skip_nan);
        }
        return;
    }
    scalars.push_back(value);
}

inline Value identity_value(ReducerKind kind, bool real_result) {
    switch (kind) {
    case ReducerKind::count:
    case ReducerKind::count_nonzero:
        return std::int64_t{0};
    case ReducerKind::sum:
        return numeric_result(0.0, real_result);
    case ReducerKind::prod:
        return numeric_result(1.0, real_result);
    case ReducerKind::any:
        return false;
    case ReducerKind::all:
        return true;
    case ReducerKind::min:
    case ReducerKind::max:
    case ReducerKind::argmin:
    case ReducerKind::argmax:
    case ReducerKind::mean:
    case ReducerKind::var:
    case ReducerKind::stddev:
    case ReducerKind::moment:
    case ReducerKind::ptp:
        return Value(nullptr);
    }
    return Value(nullptr);
}

inline Value reduce_scalars(Value::list_type values, const ReduceSettings& settings, bool real_result = false) {
    if (settings.initial) {
        real_result = real_result || prefer_real_result(*settings.initial);
    }
    for (const auto& value : values) {
        real_result = real_result || prefer_real_result(value);
    }

    if (settings.initial && !settings.initial->is_none()) {
        values.insert(values.begin(), *settings.initial);
    }

    switch (settings.kind) {
    case ReducerKind::count:
        if (values.empty() && settings.mask_identity) {
            return Value(nullptr);
        }
        return static_cast<std::int64_t>(values.size());
    case ReducerKind::count_nonzero: {
        if (values.empty() && settings.mask_identity) {
            return Value(nullptr);
        }
        std::int64_t count = 0;
        for (const auto& value : values) {
            if (value_truthy(value)) {
                ++count;
            }
        }
        return count;
    }
    case ReducerKind::sum: {
        if (values.empty() && settings.mask_identity) {
            return Value(nullptr);
        }
        double total = 0.0;
        for (const auto& value : values) {
            total += numeric_as_double(value);
        }
        return numeric_result(total, real_result);
    }
    case ReducerKind::prod: {
        if (values.empty() && settings.mask_identity) {
            return Value(nullptr);
        }
        double total = 1.0;
        for (const auto& value : values) {
            total *= numeric_as_double(value);
        }
        return numeric_result(total, real_result);
    }
    case ReducerKind::any: {
        if (values.empty() && settings.mask_identity) {
            return Value(nullptr);
        }
        return std::any_of(values.begin(), values.end(), value_truthy);
    }
    case ReducerKind::all: {
        if (values.empty() && settings.mask_identity) {
            return Value(nullptr);
        }
        return std::all_of(values.begin(), values.end(), value_truthy);
    }
    case ReducerKind::min:
    case ReducerKind::max: {
        if (values.empty()) {
            return identity_value(settings.kind, real_result);
        }
        auto best = numeric_as_double(values.front());
        for (std::size_t i = 1; i < values.size(); ++i) {
            const auto current = numeric_as_double(values[i]);
            if ((settings.kind == ReducerKind::min && current < best) ||
                (settings.kind == ReducerKind::max && current > best)) {
                best = current;
            }
        }
        return numeric_result(best, real_result);
    }
    case ReducerKind::argmin:
    case ReducerKind::argmax: {
        if (values.empty()) {
            return Value(nullptr);
        }
        auto best = numeric_as_double(values.front());
        std::int64_t best_index = 0;
        for (std::size_t i = 1; i < values.size(); ++i) {
            const auto current = numeric_as_double(values[i]);
            if ((settings.kind == ReducerKind::argmin && current < best) ||
                (settings.kind == ReducerKind::argmax && current > best)) {
                best = current;
                best_index = static_cast<std::int64_t>(i);
            }
        }
        return best_index;
    }
    case ReducerKind::mean:
    case ReducerKind::var:
    case ReducerKind::stddev:
    case ReducerKind::moment:
    case ReducerKind::ptp:
        break;
    }

    if (values.empty()) {
        return Value(nullptr);
    }

    double total = 0.0;
    for (const auto& value : values) {
        total += numeric_as_double(value);
    }
    const auto mean = total / static_cast<double>(values.size());

    if (settings.kind == ReducerKind::mean) {
        return mean;
    }
    if (settings.kind == ReducerKind::ptp) {
        auto low = numeric_as_double(values.front());
        auto high = low;
        for (std::size_t i = 1; i < values.size(); ++i) {
            const auto current = numeric_as_double(values[i]);
            low = std::min(low, current);
            high = std::max(high, current);
        }
        return high - low;
    }

    double powered = 0.0;
    for (const auto& value : values) {
        powered += std::pow(numeric_as_double(value) - mean, settings.moment_order);
    }
    if (settings.kind == ReducerKind::moment) {
        return powered / static_cast<double>(values.size());
    }

    const auto denominator = static_cast<double>(values.size()) - settings.ddof;
    if (denominator <= 0.0) {
        return Value(nullptr);
    }
    const auto variance = powered / denominator;
    if (settings.kind == ReducerKind::stddev) {
        return std::sqrt(variance);
    }
    return variance;
}

inline Value reduce_present_list(const Value::list_type& items, const ReduceSettings& settings) {
    Value::list_type scalars;
    scalars.reserve(items.size());
    bool skipped_real = false;
    for (const auto& item : items) {
        if (is_missing_for_reducer(item, settings.skip_nan)) {
            skipped_real = skipped_real || contains_real_scalar(item);
            continue;
        }
        if (value_tag(item) == ValueTag::list) {
            throw std::invalid_argument("selected reducer axis does not contain scalar values");
        }
        scalars.push_back(item);
    }
    return reduce_scalars(std::move(scalars), settings, skipped_real);
}

inline Value reduce_axis_none(const Value& value, const ReduceSettings& settings) {
    Value::list_type scalars;
    collect_scalars(value, scalars, settings.skip_nan);
    return reduce_scalars(std::move(scalars), settings, settings.skip_nan && contains_real_scalar(value));
}

inline Value reduce_axis_zero(const Value::list_type& values, const ReduceSettings& settings) {
    bool has_list = false;
    bool has_scalar = false;
    for (const auto& value : values) {
        if (is_missing_for_reducer(value, settings.skip_nan)) continue;
        if (value_tag(value) == ValueTag::list) has_list = true;
        else has_scalar = true;
    }
    if (has_list && has_scalar) {
        throw std::invalid_argument("axis reduction requires consistent list depth");
    }
    if (!has_list) return reduce_present_list(values, settings);

    std::size_t width = 0;
    for (const auto& value : values) {
        if (!is_missing_for_reducer(value, settings.skip_nan)) {
            width = std::max(width, value.as_list().size());
        }
    }
    Value::list_type result;
    result.reserve(width);
    for (std::size_t column = 0; column < width; ++column) {
        Value::list_type column_values;
        for (const auto& value : values) {
            if (is_missing_for_reducer(value, settings.skip_nan)) continue;
            const auto& items = value.as_list();
            if (column < items.size()) column_values.push_back(items[column]);
        }
        result.push_back(reduce_axis_zero(column_values, settings));
    }
    return result;
}

inline Value reduce_at_axis(const Value& value,
                            int target_axis,
                            int depth,
                            bool keepdims,
                            const ReduceSettings& settings) {
    if (value.is_none()) return Value(nullptr);
    if (value_tag(value) != ValueTag::list) {
        throw std::invalid_argument("reducer axis is deeper than an input value");
    }
    if (depth == target_axis) {
        auto result = reduce_axis_zero(value.as_list(), settings);
        if (keepdims) result = Value::list_type{std::move(result)};
        return result;
    }
    Value::list_type result;
    result.reserve(value.as_list().size());
    for (const auto& item : value.as_list()) {
        result.push_back(reduce_at_axis(item, target_axis, depth + 1, keepdims, settings));
    }
    return result;
}

inline Value reduce_array_value(const Value& array_value, std::optional<int> axis, bool keepdims, ReduceSettings settings) {
    const auto depth = array_depth(array_value);
    const auto normalized_axis = normalize_axis(axis, depth);
    if (normalized_axis == -1) {
        auto result = reduce_axis_none(array_value, settings);
        if (keepdims) {
            for (int i = 0; i < depth; ++i) result = Value::list_type{std::move(result)};
        }
        return result;
    }

    if (value_tag(array_value) != ValueTag::list) {
        throw std::invalid_argument("reducer requires an array value");
    }
    return reduce_at_axis(array_value, normalized_axis, 0, keepdims, settings);
}

inline bool sortable_less(const Value& left, const Value& right) {
    const auto left_tag = value_tag(left);
    const auto right_tag = value_tag(right);
    if (is_numeric_tag(left_tag) && is_numeric_tag(right_tag)) {
        return numeric_as_double(left) < numeric_as_double(right);
    }
    if (left_tag == ValueTag::string && right_tag == ValueTag::string) {
        return std::get<std::string>(left.storage()) < std::get<std::string>(right.storage());
    }
    throw std::invalid_argument("ak::sort requires comparable scalar values");
}

inline bool sortable_compare(const Value& left, const Value& right, bool ascending) {
    if (left.is_none() && right.is_none()) {
        return false;
    }
    if (left.is_none()) {
        return false;
    }
    if (right.is_none()) {
        return true;
    }
    return ascending ? sortable_less(left, right) : sortable_less(right, left);
}

inline Value::list_type sort_list(Value::list_type items, bool ascending) {
    std::stable_sort(items.begin(), items.end(), [ascending](const Value& left, const Value& right) {
        return sortable_compare(left, right, ascending);
    });
    return items;
}

inline Value::list_type argsort_list(const Value::list_type& items, bool ascending) {
    std::vector<std::size_t> indices(items.size());
    for (std::size_t i = 0; i < indices.size(); ++i) {
        indices[i] = i;
    }
    std::stable_sort(indices.begin(), indices.end(), [&items, ascending](std::size_t left, std::size_t right) {
        return sortable_compare(items[left], items[right], ascending);
    });

    Value::list_type result;
    result.reserve(indices.size());
    for (const auto index : indices) {
        result.emplace_back(static_cast<std::int64_t>(index));
    }
    return result;
}

inline Value::list_type sort_axis_zero_ragged(const Value::list_type& rows,
                                              bool ascending,
                                              bool return_indices) {
    Value::list_type result = rows;
    std::size_t width = 0;
    for (const auto& row : rows) {
        if (!row.is_none() && value_tag(row) == ValueTag::list) width = std::max(width, row.as_list().size());
    }
    for (std::size_t column = 0; column < width; ++column) {
        std::vector<std::size_t> target_rows;
        Value::list_type column_values;
        for (std::size_t row = 0; row < rows.size(); ++row) {
            if (rows[row].is_none() || value_tag(rows[row]) != ValueTag::list || column >= rows[row].as_list().size()) continue;
            target_rows.push_back(row);
            column_values.push_back(rows[row].as_list()[column]);
        }
        Value::list_type sorted;
        const auto nested = std::any_of(column_values.begin(), column_values.end(), [](const auto& value) {
            return !value.is_none() && value_tag(value) == ValueTag::list;
        });
        if (nested) {
            sorted = sort_axis_zero_ragged(column_values, ascending, return_indices);
        } else {
            sorted = return_indices ? argsort_list(column_values, ascending) : sort_list(column_values, ascending);
            if (return_indices) {
                for (auto& value : sorted) {
                    const auto local = static_cast<std::size_t>(std::get<std::int64_t>(value.storage()));
                    value = static_cast<std::int64_t>(target_rows[local]);
                }
            }
        }
        for (std::size_t i = 0; i < target_rows.size(); ++i) {
            auto row = result[target_rows[i]].as_list();
            row[column] = sorted[i];
            result[target_rows[i]] = std::move(row);
        }
    }
    return result;
}

inline Value sort_value(const Value& array_value, int axis, bool ascending, bool return_indices) {
    const auto depth = array_depth(array_value);
    auto normalized_axis = axis;
    if (normalized_axis < 0) {
        normalized_axis += depth;
    }
    if (normalized_axis < 0 || normalized_axis >= depth) {
        throw std::invalid_argument("sort axis is outside the implemented layout depth");
    }
    if (value_tag(array_value) != ValueTag::list) {
        throw std::invalid_argument("ak::sort requires an array value");
    }

    const auto recurse = [&](const auto& self, const Value& value, int current_depth) -> Value {
        if (value.is_none()) return Value(nullptr);
        if (value_tag(value) != ValueTag::list) {
            throw std::invalid_argument("sort axis is deeper than an input value");
        }
        if (current_depth == normalized_axis) {
            const auto nested = std::any_of(value.as_list().begin(), value.as_list().end(), [](const auto& item) {
                return !item.is_none() && value_tag(item) == ValueTag::list;
            });
            if (nested) return sort_axis_zero_ragged(value.as_list(), ascending, return_indices);
            return return_indices ? argsort_list(value.as_list(), ascending) : sort_list(value.as_list(), ascending);
        }
        Value::list_type result;
        result.reserve(value.as_list().size());
        for (const auto& item : value.as_list()) result.push_back(self(self, item, current_depth + 1));
        return result;
    };
    return recurse(recurse, array_value, 0);
}

inline Value softmax_list(const Value::list_type& items) {
    double maximum = -std::numeric_limits<double>::infinity();
    bool has_value = false;
    for (const auto& item : items) {
        if (is_missing_for_reducer(item, true)) {
            continue;
        }
        maximum = std::max(maximum, numeric_as_double(item));
        has_value = true;
    }
    if (!has_value) {
        Value::list_type empty_or_missing;
        empty_or_missing.reserve(items.size());
        for (const auto& item : items) {
            empty_or_missing.push_back(item.is_none() ? Value(nullptr) : Value(std::numeric_limits<double>::quiet_NaN()));
        }
        return empty_or_missing;
    }

    double denominator = 0.0;
    for (const auto& item : items) {
        if (!is_missing_for_reducer(item, true)) {
            denominator += std::exp(numeric_as_double(item) - maximum);
        }
    }

    Value::list_type result;
    result.reserve(items.size());
    for (const auto& item : items) {
        if (item.is_none()) {
            result.emplace_back(nullptr);
        } else if (is_nan_value(item)) {
            result.emplace_back(std::numeric_limits<double>::quiet_NaN());
        } else {
            result.emplace_back(std::exp(numeric_as_double(item) - maximum) / denominator);
        }
    }
    return result;
}

inline Value softmax_value(const Value& array_value, int axis) {
    const auto depth = array_depth(array_value);
    auto normalized_axis = axis;
    if (normalized_axis < 0) {
        normalized_axis += depth;
    }
    if (normalized_axis < 0 || normalized_axis >= depth) {
        throw std::invalid_argument("softmax axis is outside the implemented layout depth");
    }
    if (value_tag(array_value) != ValueTag::list) {
        throw std::invalid_argument("ak::softmax requires an array value");
    }

    const auto recurse = [&](const auto& self, const Value& value, int current_depth) -> Value {
        if (value.is_none()) return Value(nullptr);
        if (value_tag(value) != ValueTag::list) {
            throw std::invalid_argument("softmax axis is deeper than an input value");
        }
        if (current_depth == normalized_axis) return softmax_list(value.as_list());
        Value::list_type result;
        result.reserve(value.as_list().size());
        for (const auto& item : value.as_list()) result.push_back(self(self, item, current_depth + 1));
        return result;
    };
    return recurse(recurse, array_value, 0);
}

inline Value make_record_value(const std::vector<std::string>& names,
                               const std::vector<Value>& values,
                               bool is_tuple = false) {
    if (names.size() != values.size()) {
        throw std::invalid_argument("record names and values must have matching lengths");
    }
    Value::record_type record;
    record.is_tuple = is_tuple;
    record.fields = names;
    record.values = values;
    return record;
}

inline bool all_list_values(const std::vector<Value>& values) {
    if (values.empty()) {
        return false;
    }
    for (const auto& value : values) {
        if (value.is_none() || value_tag(value) != ValueTag::list) {
            return false;
        }
    }
    return true;
}

inline Value zip_values(const std::vector<std::string>& names,
                        const std::vector<Value>& values,
                        bool is_tuple,
                        std::optional<int> depth_limit,
                        int depth) {
    if (names.empty()) {
        throw std::invalid_argument("ak::zip requires at least one field");
    }
    if (depth_limit && depth >= *depth_limit) {
        return make_record_value(names, values, is_tuple);
    }
    if (!all_list_values(values)) {
        return make_record_value(names, values, is_tuple);
    }

    const auto row_length = values.front().as_list().size();
    for (const auto& value : values) {
        if (value.as_list().size() != row_length) {
            return make_record_value(names, values, is_tuple);
        }
    }

    Value::list_type result;
    result.reserve(row_length);
    for (std::size_t i = 0; i < row_length; ++i) {
        std::vector<Value> nested_values;
        nested_values.reserve(values.size());
        for (const auto& value : values) {
            nested_values.push_back(value.as_list()[i]);
        }
        result.push_back(zip_values(names, nested_values, is_tuple, depth_limit, depth + 1));
    }
    return result;
}

inline Value with_field_value(const Value& base, const Value& what, const std::string& name) {
    if (base.is_none()) {
        return Value(nullptr);
    }
    if (value_tag(base) == ValueTag::list) {
        if (value_tag(what) != ValueTag::list) {
            throw std::invalid_argument("ak::with_field requires matching nested list structure");
        }
        const auto& base_items = base.as_list();
        const auto& what_items = what.as_list();
        if (base_items.size() != what_items.size()) {
            throw std::invalid_argument("ak::with_field requires matching list lengths");
        }
        Value::list_type result;
        result.reserve(base_items.size());
        for (std::size_t i = 0; i < base_items.size(); ++i) {
            result.push_back(with_field_value(base_items[i], what_items[i], name));
        }
        return result;
    }
    if (value_tag(base) != ValueTag::record) {
        throw std::invalid_argument("ak::with_field requires records");
    }

    auto record = base.as_record();
    if (record.is_tuple) {
        throw std::invalid_argument("ak::with_field requires named records, not tuples");
    }
    const auto found = std::find(record.fields.begin(), record.fields.end(), name);
    if (found == record.fields.end()) {
        record.fields.push_back(name);
        record.values.push_back(what);
    } else {
        const auto index = static_cast<std::size_t>(std::distance(record.fields.begin(), found));
        record.values[index] = what;
    }
    return record;
}

inline Value without_field_value(const Value& base, const std::string& name) {
    if (base.is_none()) {
        return Value(nullptr);
    }
    if (value_tag(base) == ValueTag::list) {
        Value::list_type result;
        result.reserve(base.as_list().size());
        for (const auto& item : base.as_list()) {
            result.push_back(without_field_value(item, name));
        }
        return result;
    }
    if (value_tag(base) != ValueTag::record) {
        throw std::invalid_argument("ak::without_field requires records");
    }

    const auto& source = base.as_record();
    if (source.is_tuple) {
        throw std::invalid_argument("ak::without_field requires named records, not tuples");
    }
    Value::record_type record;
    record.is_tuple = false;
    bool removed = false;
    for (std::size_t i = 0; i < source.fields.size(); ++i) {
        if (source.fields[i] == name) {
            removed = true;
            continue;
        }
        record.fields.push_back(source.fields[i]);
        record.values.push_back(source.values[i]);
    }
    if (!removed) {
        throw std::out_of_range("record field does not exist: " + name);
    }
    return record;
}

inline bool is_flat_scalar_list(const Value& value) {
    if (value_tag(value) != ValueTag::list) return false;
    return std::all_of(value.as_list().begin(), value.as_list().end(), [](const auto& item) {
        return item.is_none() || value_tag(item) != ValueTag::list;
    });
}

inline bool is_nested_list(const Value& value) {
    return value_tag(value) == ValueTag::list &&
           std::any_of(value.as_list().begin(), value.as_list().end(), [](const auto& item) {
               return !item.is_none() && value_tag(item) == ValueTag::list;
           });
}

inline Value broadcast_flat_columns(const Value& flat, const Value& shape) {
    if (shape.is_none()) return Value(nullptr);
    if (value_tag(shape) != ValueTag::list) {
        throw std::invalid_argument("ragged axis-0 broadcast requires nested list shape");
    }
    const auto& columns = flat.as_list();
    const auto& shape_values = shape.as_list();
    const auto deeper = std::any_of(shape_values.begin(), shape_values.end(), [](const auto& item) {
        return !item.is_none() && value_tag(item) == ValueTag::list;
    });
    Value::list_type result;
    result.reserve(shape_values.size());
    if (!deeper) {
        if (shape_values.size() > columns.size()) {
            throw std::invalid_argument("ragged axis-0 broadcast has fewer column values than a row");
        }
        result.insert(result.end(), columns.begin(), columns.begin() + static_cast<std::ptrdiff_t>(shape_values.size()));
        return result;
    }
    for (const auto& item : shape_values) result.push_back(broadcast_flat_columns(flat, item));
    return result;
}

inline std::vector<Value> broadcast_value_list(const std::vector<Value>& values) {
    if (values.empty()) {
        throw std::invalid_argument("ak::broadcast_arrays requires at least one input");
    }

    for (const auto& value : values) {
        if (value.is_none()) {
            return std::vector<Value>(values.size(), Value(nullptr));
        }
    }

    std::optional<std::size_t> list_length;
    for (const auto& value : values) {
        if (value_tag(value) != ValueTag::list) {
            continue;
        }
        const auto size = value.as_list().size();
        if (!list_length) {
            list_length = size;
        } else if (*list_length != size) {
            std::optional<std::size_t> nested_index;
            for (std::size_t i = 0; i < values.size(); ++i) if (is_nested_list(values[i])) nested_index = i;
            if (!nested_index) throw std::invalid_argument("ak::broadcast_arrays requires compatible list lengths");
            std::vector<Value> normalized = values;
            bool changed = false;
            for (std::size_t i = 0; i < normalized.size(); ++i) {
                if (i != *nested_index && is_flat_scalar_list(normalized[i]) &&
                    normalized[i].as_list().size() != values[*nested_index].as_list().size()) {
                    normalized[i] = broadcast_flat_columns(normalized[i], values[*nested_index]);
                    changed = true;
                }
            }
            if (!changed) throw std::invalid_argument("ak::broadcast_arrays requires compatible list lengths");
            return broadcast_value_list(normalized);
        }
    }

    if (!list_length) {
        return values;
    }

    std::vector<Value::list_type> output_lists(values.size());
    for (auto& output : output_lists) {
        output.reserve(*list_length);
    }

    for (std::size_t i = 0; i < *list_length; ++i) {
        std::vector<Value> items;
        items.reserve(values.size());
        for (const auto& value : values) {
            if (value_tag(value) == ValueTag::list) {
                items.push_back(value.as_list()[i]);
            } else {
                items.push_back(value);
            }
        }

        const auto broadcasted = broadcast_value_list(items);
        for (std::size_t output = 0; output < broadcasted.size(); ++output) {
            output_lists[output].push_back(broadcasted[output]);
        }
    }

    std::vector<Value> result;
    result.reserve(values.size());
    for (auto& output : output_lists) {
        result.emplace_back(std::move(output));
    }
    return result;
}

inline bool numeric_equal(const Value& left, const Value& right) {
    return numeric_as_double(left) == numeric_as_double(right);
}

inline Value binary_scalar_value(const Value& left, const Value& right, BinaryOpKind kind) {
    const auto left_tag = value_tag(left);
    const auto right_tag = value_tag(right);

    switch (kind) {
    case BinaryOpKind::add:
    case BinaryOpKind::subtract:
    case BinaryOpKind::multiply: {
        if (!is_numeric_tag(left_tag) || !is_numeric_tag(right_tag)) {
            throw std::invalid_argument("elementwise arithmetic requires numeric values");
        }
        const auto real_result = left_tag == ValueTag::real || right_tag == ValueTag::real;
        const auto left_value = numeric_as_double(left);
        const auto right_value = numeric_as_double(right);
        if (kind == BinaryOpKind::add) {
            return numeric_result(left_value + right_value, real_result);
        }
        if (kind == BinaryOpKind::subtract) {
            return numeric_result(left_value - right_value, real_result);
        }
        return numeric_result(left_value * right_value, real_result);
    }
    case BinaryOpKind::divide:
        if (!is_numeric_tag(left_tag) || !is_numeric_tag(right_tag)) {
            throw std::invalid_argument("elementwise division requires numeric values");
        }
        return numeric_as_double(left) / numeric_as_double(right);
    case BinaryOpKind::equal:
    case BinaryOpKind::not_equal: {
        bool equal = false;
        if (is_numeric_tag(left_tag) && is_numeric_tag(right_tag)) {
            equal = numeric_equal(left, right);
        } else {
            equal = left == right;
        }
        return kind == BinaryOpKind::equal ? equal : !equal;
    }
    case BinaryOpKind::less:
    case BinaryOpKind::less_equal:
    case BinaryOpKind::greater:
    case BinaryOpKind::greater_equal: {
        bool less = false;
        bool greater = false;
        if (is_numeric_tag(left_tag) && is_numeric_tag(right_tag)) {
            const auto left_value = numeric_as_double(left);
            const auto right_value = numeric_as_double(right);
            less = left_value < right_value;
            greater = left_value > right_value;
        } else if (left_tag == ValueTag::string && right_tag == ValueTag::string) {
            const auto& left_value = std::get<std::string>(left.storage());
            const auto& right_value = std::get<std::string>(right.storage());
            less = left_value < right_value;
            greater = left_value > right_value;
        } else {
            throw std::invalid_argument("elementwise comparison requires compatible scalar values");
        }
        if (kind == BinaryOpKind::less) {
            return less;
        }
        if (kind == BinaryOpKind::less_equal) {
            return !greater;
        }
        if (kind == BinaryOpKind::greater) {
            return greater;
        }
        return !less;
    }
    case BinaryOpKind::logical_and:
        return value_truthy(left) && value_truthy(right);
    case BinaryOpKind::logical_or:
        return value_truthy(left) || value_truthy(right);
    }

    throw std::invalid_argument("unsupported elementwise operation");
}

inline void validate_matching_record_fields(const Value::record_type& left, const Value::record_type& right) {
    if (left.is_tuple != right.is_tuple || left.fields.size() != right.fields.size()) {
        throw std::invalid_argument("record broadcasting requires matching fields");
    }
    for (const auto& field : left.fields) {
        if (std::find(right.fields.begin(), right.fields.end(), field) == right.fields.end()) {
            throw std::invalid_argument("record broadcasting requires matching fields");
        }
    }
}

inline std::size_t record_field_index(const Value::record_type& record, const std::string& name) {
    const auto found = std::find(record.fields.begin(), record.fields.end(), name);
    if (found == record.fields.end()) {
        throw std::invalid_argument("record field does not exist: " + name);
    }
    return static_cast<std::size_t>(std::distance(record.fields.begin(), found));
}

inline Value binary_value(const Value& left, const Value& right, BinaryOpKind kind) {
    if (left.is_none() || right.is_none()) {
        return Value(nullptr);
    }

    const auto left_tag = value_tag(left);
    const auto right_tag = value_tag(right);
    if (left_tag == ValueTag::list || right_tag == ValueTag::list) {
        const auto broadcasted = broadcast_value_list({left, right});
        const auto& left_values = broadcasted[0].as_list();
        const auto& right_values = broadcasted[1].as_list();
        Value::list_type result;
        result.reserve(left_values.size());
        for (std::size_t i = 0; i < left_values.size(); ++i) {
            result.push_back(binary_value(left_values[i], right_values[i], kind));
        }
        return result;
    }

    if (left_tag == ValueTag::record || right_tag == ValueTag::record) {
        if (left_tag != ValueTag::record || right_tag != ValueTag::record) {
            throw std::invalid_argument("elementwise record operations require record pairs");
        }
        const auto& left_record = left.as_record();
        const auto& right_record = right.as_record();
        validate_matching_record_fields(left_record, right_record);

        Value::record_type result;
        result.is_tuple = left_record.is_tuple;
        result.fields = left_record.fields;
        result.values.reserve(left_record.values.size());
        for (std::size_t i = 0; i < left_record.fields.size(); ++i) {
            const auto right_index = record_field_index(right_record, left_record.fields[i]);
            result.values.push_back(binary_value(left_record.values[i], right_record.values[right_index], kind));
        }
        return result;
    }

    return binary_scalar_value(left, right, kind);
}

inline Value logical_not_value(const Value& value) {
    if (value.is_none()) {
        return Value(nullptr);
    }
    if (value_tag(value) == ValueTag::list) {
        Value::list_type result;
        result.reserve(value.as_list().size());
        for (const auto& item : value.as_list()) {
            result.push_back(logical_not_value(item));
        }
        return result;
    }
    return !value_truthy(value);
}

inline Value where_value(const Value& condition, const Value& left, const Value& right) {
    if (condition.is_none()) {
        return Value(nullptr);
    }
    if (value_tag(condition) == ValueTag::list || value_tag(left) == ValueTag::list || value_tag(right) == ValueTag::list) {
        const auto broadcasted = broadcast_value_list({condition, left, right});
        const auto& conditions = broadcasted[0].as_list();
        const auto& left_values = broadcasted[1].as_list();
        const auto& right_values = broadcasted[2].as_list();
        Value::list_type result;
        result.reserve(conditions.size());
        for (std::size_t i = 0; i < conditions.size(); ++i) {
            result.push_back(where_value(conditions[i], left_values[i], right_values[i]));
        }
        return result;
    }
    if (value_tag(condition) != ValueTag::boolean) {
        throw std::invalid_argument("ak::where requires boolean conditions");
    }
    return std::get<bool>(condition.storage()) ? left : right;
}

inline Value isclose_value(const Value& left, const Value& right, double rtol, double atol, bool equal_nan) {
    if (left.is_none() || right.is_none()) {
        return Value(nullptr);
    }
    const auto left_tag = value_tag(left);
    const auto right_tag = value_tag(right);
    if (left_tag == ValueTag::list || right_tag == ValueTag::list) {
        const auto broadcasted = broadcast_value_list({left, right});
        const auto& left_values = broadcasted[0].as_list();
        const auto& right_values = broadcasted[1].as_list();
        Value::list_type result;
        result.reserve(left_values.size());
        for (std::size_t i = 0; i < left_values.size(); ++i) {
            result.push_back(isclose_value(left_values[i], right_values[i], rtol, atol, equal_nan));
        }
        return result;
    }
    if (!is_numeric_tag(left_tag) || !is_numeric_tag(right_tag)) {
        throw std::invalid_argument("ak::isclose requires numeric values");
    }
    const auto left_value = numeric_as_double(left);
    const auto right_value = numeric_as_double(right);
    if (std::isnan(left_value) || std::isnan(right_value)) {
        return equal_nan && std::isnan(left_value) && std::isnan(right_value);
    }
    return std::fabs(left_value - right_value) <= (atol + rtol * std::fabs(right_value));
}

inline bool equal_value(const Value& left, const Value& right, bool equal_nan) {
    const auto left_tag = value_tag(left);
    const auto right_tag = value_tag(right);
    if (left.is_none() || right.is_none()) {
        return left.is_none() && right.is_none();
    }
    if (left_tag == ValueTag::list || right_tag == ValueTag::list) {
        if (left_tag != ValueTag::list || right_tag != ValueTag::list) {
            return false;
        }
        const auto& left_values = left.as_list();
        const auto& right_values = right.as_list();
        if (left_values.size() != right_values.size()) {
            return false;
        }
        for (std::size_t i = 0; i < left_values.size(); ++i) {
            if (!equal_value(left_values[i], right_values[i], equal_nan)) {
                return false;
            }
        }
        return true;
    }
    if (left_tag == ValueTag::record || right_tag == ValueTag::record) {
        if (left_tag != ValueTag::record || right_tag != ValueTag::record) {
            return false;
        }
        const auto& left_record = left.as_record();
        const auto& right_record = right.as_record();
        if (left_record.is_tuple != right_record.is_tuple || left_record.fields != right_record.fields ||
            left_record.values.size() != right_record.values.size()) {
            return false;
        }
        for (std::size_t i = 0; i < left_record.values.size(); ++i) {
            if (!equal_value(left_record.values[i], right_record.values[i], equal_nan)) {
                return false;
            }
        }
        return true;
    }
    if (is_numeric_tag(left_tag) && is_numeric_tag(right_tag)) {
        const auto left_value = numeric_as_double(left);
        const auto right_value = numeric_as_double(right);
        if (std::isnan(left_value) || std::isnan(right_value)) {
            return equal_nan && std::isnan(left_value) && std::isnan(right_value);
        }
        return left_value == right_value;
    }
    return left == right;
}

inline bool almost_equal_value(const Value& left, const Value& right, double rtol, double atol, bool equal_nan) {
    const auto left_tag = value_tag(left);
    const auto right_tag = value_tag(right);
    if (left.is_none() || right.is_none()) {
        return left.is_none() && right.is_none();
    }
    if (left_tag == ValueTag::list || right_tag == ValueTag::list) {
        if (left_tag != ValueTag::list || right_tag != ValueTag::list) {
            return false;
        }
        const auto& left_values = left.as_list();
        const auto& right_values = right.as_list();
        if (left_values.size() != right_values.size()) {
            return false;
        }
        for (std::size_t i = 0; i < left_values.size(); ++i) {
            if (!almost_equal_value(left_values[i], right_values[i], rtol, atol, equal_nan)) {
                return false;
            }
        }
        return true;
    }
    if (left_tag == ValueTag::record || right_tag == ValueTag::record) {
        if (left_tag != ValueTag::record || right_tag != ValueTag::record) {
            return false;
        }
        const auto& left_record = left.as_record();
        const auto& right_record = right.as_record();
        if (left_record.is_tuple != right_record.is_tuple || left_record.fields != right_record.fields ||
            left_record.values.size() != right_record.values.size()) {
            return false;
        }
        for (std::size_t i = 0; i < left_record.values.size(); ++i) {
            if (!almost_equal_value(left_record.values[i], right_record.values[i], rtol, atol, equal_nan)) {
                return false;
            }
        }
        return true;
    }
    if (is_numeric_tag(left_tag) && is_numeric_tag(right_tag)) {
        const auto left_value = numeric_as_double(left);
        const auto right_value = numeric_as_double(right);
        if (std::isnan(left_value) || std::isnan(right_value)) {
            return equal_nan && std::isnan(left_value) && std::isnan(right_value);
        }
        return std::fabs(left_value - right_value) <= (atol + rtol * std::fabs(right_value));
    }
    return left == right;
}

inline Value like_value(const Value& value, const Value& fill) {
    if (value.is_none()) {
        return Value(nullptr);
    }
    const auto tag = value_tag(value);
    if (tag == ValueTag::list) {
        Value::list_type result;
        result.reserve(value.as_list().size());
        for (const auto& item : value.as_list()) {
            result.push_back(like_value(item, fill));
        }
        return result;
    }
    if (tag == ValueTag::record) {
        const auto& source = value.as_record();
        Value::record_type result;
        result.is_tuple = source.is_tuple;
        result.fields = source.fields;
        result.values.reserve(source.values.size());
        for (const auto& item : source.values) {
            result.values.push_back(like_value(item, fill));
        }
        return result;
    }
    return fill;
}

inline Value zero_for_value(const Value& value) {
    const auto tag = value_tag(value);
    if (tag == ValueTag::real) {
        return 0.0;
    }
    if (tag == ValueTag::boolean) {
        return false;
    }
    if (tag == ValueTag::integer) {
        return std::int64_t{0};
    }
    throw std::invalid_argument("ak::zeros_like requires numeric or boolean values");
}

inline Value one_for_value(const Value& value) {
    const auto tag = value_tag(value);
    if (tag == ValueTag::real) {
        return 1.0;
    }
    if (tag == ValueTag::boolean) {
        return true;
    }
    if (tag == ValueTag::integer) {
        return std::int64_t{1};
    }
    throw std::invalid_argument("ak::ones_like requires numeric or boolean values");
}

inline Value zeros_like_value(const Value& value) {
    if (value.is_none()) {
        return Value(nullptr);
    }
    if (value_tag(value) == ValueTag::list) {
        Value::list_type result;
        result.reserve(value.as_list().size());
        for (const auto& item : value.as_list()) {
            result.push_back(zeros_like_value(item));
        }
        return result;
    }
    if (value_tag(value) == ValueTag::record) {
        const auto& source = value.as_record();
        Value::record_type result;
        result.is_tuple = source.is_tuple;
        result.fields = source.fields;
        result.values.reserve(source.values.size());
        for (const auto& item : source.values) {
            result.values.push_back(zeros_like_value(item));
        }
        return result;
    }
    return zero_for_value(value);
}

inline Value ones_like_value(const Value& value) {
    if (value.is_none()) {
        return Value(nullptr);
    }
    if (value_tag(value) == ValueTag::list) {
        Value::list_type result;
        result.reserve(value.as_list().size());
        for (const auto& item : value.as_list()) {
            result.push_back(ones_like_value(item));
        }
        return result;
    }
    if (value_tag(value) == ValueTag::record) {
        const auto& source = value.as_record();
        Value::record_type result;
        result.is_tuple = source.is_tuple;
        result.fields = source.fields;
        result.values.reserve(source.values.size());
        for (const auto& item : source.values) {
            result.values.push_back(ones_like_value(item));
        }
        return result;
    }
    return one_for_value(value);
}

inline std::pair<Value, Value> broadcast_fields_value(const Value& left, const Value& right) {
    if (left.is_none() || right.is_none()) {
        return {Value(nullptr), Value(nullptr)};
    }
    const auto left_tag = value_tag(left);
    const auto right_tag = value_tag(right);
    if (left_tag == ValueTag::list || right_tag == ValueTag::list) {
        const auto broadcasted = broadcast_value_list({left, right});
        const auto& left_values = broadcasted[0].as_list();
        const auto& right_values = broadcasted[1].as_list();
        Value::list_type left_result;
        Value::list_type right_result;
        left_result.reserve(left_values.size());
        right_result.reserve(right_values.size());
        for (std::size_t i = 0; i < left_values.size(); ++i) {
            auto nested = broadcast_fields_value(left_values[i], right_values[i]);
            left_result.push_back(std::move(nested.first));
            right_result.push_back(std::move(nested.second));
        }
        return {Value(std::move(left_result)), Value(std::move(right_result))};
    }
    if (left_tag != ValueTag::record || right_tag != ValueTag::record) {
        return {left, right};
    }

    const auto& left_record = left.as_record();
    const auto& right_record = right.as_record();
    validate_matching_record_fields(left_record, right_record);

    Value::record_type left_result;
    Value::record_type right_result;
    left_result.is_tuple = left_record.is_tuple;
    right_result.is_tuple = left_record.is_tuple;
    left_result.fields = left_record.fields;
    right_result.fields = left_record.fields;
    left_result.values.reserve(left_record.values.size());
    right_result.values.reserve(left_record.values.size());

    for (std::size_t i = 0; i < left_record.fields.size(); ++i) {
        const auto right_index = record_field_index(right_record, left_record.fields[i]);
        auto nested = broadcast_fields_value(left_record.values[i], right_record.values[right_index]);
        left_result.values.push_back(std::move(nested.first));
        right_result.values.push_back(std::move(nested.second));
    }
    return {Value(std::move(left_result)), Value(std::move(right_result))};
}

template <typename T>
const std::vector<T>& require_buffer(const BufferMap& buffers, const std::string& key) {
    const auto found = buffers.find(key);
    if (found == buffers.end()) {
        throw std::invalid_argument("missing buffer: " + key);
    }
    const auto* values = std::get_if<std::vector<T>>(&found->second);
    if (values == nullptr) {
        throw std::invalid_argument("buffer has unexpected type: " + key);
    }
    return *values;
}

inline std::vector<std::size_t> offsets_from_buffer(const BufferMap& buffers, const std::string& key) {
    const auto& raw = require_buffer<std::int64_t>(buffers, key);
    std::vector<std::size_t> result;
    result.reserve(raw.size());
    for (const auto value : raw) {
        if (value < 0) {
            throw std::invalid_argument("offset buffers must not contain negative values");
        }
        result.push_back(static_cast<std::size_t>(value));
    }
    return result;
}

inline std::vector<std::ptrdiff_t> signed_index_from_buffer(const BufferMap& buffers, const std::string& key) {
    const auto& raw = require_buffer<std::int64_t>(buffers, key);
    std::vector<std::ptrdiff_t> result;
    result.reserve(raw.size());
    for (const auto value : raw) {
        result.push_back(static_cast<std::ptrdiff_t>(value));
    }
    return result;
}

inline void require_form_length(const Form& form, std::size_t length) {
    if (form.length != length) {
        throw std::invalid_argument("form length does not match requested array length");
    }
}

inline const Form& require_single_content(const Form& form) {
    if (form.contents.size() != 1) {
        throw std::invalid_argument("form requires exactly one content");
    }
    return form.contents.front();
}

template <typename T>
std::shared_ptr<const Content> list_offset_from_typed_content(const std::shared_ptr<const Content>& content,
                                                             std::vector<std::size_t> offsets) {
    const auto typed = std::dynamic_pointer_cast<const NumpyArray<T>>(content);
    if (!typed) {
        return nullptr;
    }
    return std::make_shared<ListOffsetArray<T>>(typed, std::move(offsets));
}

template <typename T>
std::shared_ptr<const Content> regular_from_typed_content(const std::shared_ptr<const Content>& content,
                                                          std::size_t size,
                                                          std::size_t length) {
    const auto typed = std::dynamic_pointer_cast<const NumpyArray<T>>(content);
    if (!typed) {
        return nullptr;
    }
    return std::make_shared<RegularArray<T>>(typed, size, length);
}

inline std::shared_ptr<const Content> primitive_content_from_buffers(const Form& form, const BufferMap& buffers) {
    const auto key = form.key + "-data";
    if (form.primitive == "bool") {
        const auto values = require_buffer<bool>(buffers, key);
        if (values.size() != form.length) {
            throw std::invalid_argument("primitive buffer length does not match form length");
        }
        return std::make_shared<NumpyArray<bool>>(values);
    }
    if (form.primitive == "int64") {
        const auto values = require_buffer<std::int64_t>(buffers, key);
        if (values.size() != form.length) {
            throw std::invalid_argument("primitive buffer length does not match form length");
        }
        return std::make_shared<NumpyArray<std::int64_t>>(values);
    }
    if (form.primitive == "uint64") {
        const auto values = require_buffer<std::uint64_t>(buffers, key);
        if (values.size() != form.length) {
            throw std::invalid_argument("primitive buffer length does not match form length");
        }
        return std::make_shared<NumpyArray<std::uint64_t>>(values);
    }
    if (form.primitive == "float32") {
        const auto values = require_buffer<float>(buffers, key);
        if (values.size() != form.length) {
            throw std::invalid_argument("primitive buffer length does not match form length");
        }
        return std::make_shared<NumpyArray<float>>(values);
    }
    if (form.primitive == "float64") {
        const auto values = require_buffer<double>(buffers, key);
        if (values.size() != form.length) {
            throw std::invalid_argument("primitive buffer length does not match form length");
        }
        return std::make_shared<NumpyArray<double>>(values);
    }
    if (form.primitive == "uint8") {
        const auto values = require_buffer<std::uint8_t>(buffers, key);
        if (values.size() != form.length) {
            throw std::invalid_argument("primitive buffer length does not match form length");
        }
        return std::make_shared<NumpyArray<std::uint8_t>>(values);
    }
    if (form.primitive == "string") {
        const auto values = require_buffer<std::string>(buffers, key);
        if (values.size() != form.length) {
            throw std::invalid_argument("primitive buffer length does not match form length");
        }
        return std::make_shared<NumpyArray<std::string>>(values);
    }
    throw std::invalid_argument("unsupported primitive form: " + form.primitive);
}

inline std::shared_ptr<const Content> content_from_buffers(const Form& form,
                                                           std::size_t length,
                                                           const BufferMap& buffers) {
    require_form_length(form, length);

    switch (form.kind) {
    case FormKind::empty:
        if (length != 0) {
            throw std::invalid_argument("empty form length must be zero");
        }
        return std::make_shared<EmptyArray>();
    case FormKind::numpy:
        return primitive_content_from_buffers(form, buffers);
    case FormKind::list: {
        const auto& content_form = require_single_content(form);
        auto starts = offsets_from_buffer(buffers, form.key + "-starts");
        auto stops = offsets_from_buffer(buffers, form.key + "-stops");
        if (starts.size() != length || stops.size() != length) {
            throw std::invalid_argument("list starts and stops lengths must match array length");
        }
        return std::make_shared<ListArray>(
            std::move(starts), std::move(stops),
            content_from_buffers(content_form, content_form.length, buffers));
    }
    case FormKind::list_offset: {
        const auto& content_form = require_single_content(form);
        auto offsets = offsets_from_buffer(buffers, form.key + "-offsets");
        if (offsets.size() != length + 1) {
            throw std::invalid_argument("list-offset buffer length must equal array length plus one");
        }
        if (offsets.empty() || offsets.front() != 0 || !std::is_sorted(offsets.begin(), offsets.end())) {
            throw std::invalid_argument("list-offset buffer must start at zero and be monotonic");
        }
        const auto string_parameter = form.parameters.find("__array__");
        if (string_parameter != form.parameters.end() && string_parameter->second == "string") {
            if (content_form.kind != FormKind::numpy || content_form.primitive != "uint8") {
                throw std::invalid_argument("string form requires uint8 primitive content");
            }
            const auto bytes = require_buffer<std::uint8_t>(buffers, content_form.key + "-data");
            if (bytes.size() != content_form.length || offsets.back() != bytes.size()) {
                throw std::invalid_argument("string byte buffer length does not match string offsets");
            }
            return std::make_shared<StringArray>(bytes, std::move(offsets));
        }
        auto content = content_from_buffers(content_form, offsets.back(), buffers);
        if (auto layout = list_offset_from_typed_content<bool>(content, offsets)) {
            return layout;
        }
        if (auto layout = list_offset_from_typed_content<std::int64_t>(content, offsets)) {
            return layout;
        }
        if (auto layout = list_offset_from_typed_content<std::uint64_t>(content, offsets)) {
            return layout;
        }
        if (auto layout = list_offset_from_typed_content<float>(content, offsets)) {
            return layout;
        }
        if (auto layout = list_offset_from_typed_content<double>(content, offsets)) {
            return layout;
        }
        if (auto layout = list_offset_from_typed_content<std::string>(content, offsets)) {
            return layout;
        }
        return std::make_shared<ListOffsetContentArray>(content, std::move(offsets));
    }
    case FormKind::regular: {
        const auto& content_form = require_single_content(form);
        const auto content_length = form.size == 0 ? 0 : length * form.size;
        auto content = content_from_buffers(content_form, content_length, buffers);
        if (auto layout = regular_from_typed_content<bool>(content, form.size, length)) {
            return layout;
        }
        if (auto layout = regular_from_typed_content<std::int64_t>(content, form.size, length)) {
            return layout;
        }
        if (auto layout = regular_from_typed_content<std::uint64_t>(content, form.size, length)) {
            return layout;
        }
        if (auto layout = regular_from_typed_content<float>(content, form.size, length)) {
            return layout;
        }
        if (auto layout = regular_from_typed_content<double>(content, form.size, length)) {
            return layout;
        }
        if (const auto strings = std::dynamic_pointer_cast<const StringArray>(content)) {
            return std::make_shared<RegularArray<std::string>>(strings->strings(), form.size, length);
        }
        if (auto layout = regular_from_typed_content<std::string>(content, form.size, length)) {
            return layout;
        }
        return std::make_shared<RegularContentArray>(std::move(content), form.size, length);
    }
    case FormKind::indexed: {
        const auto& content_form = require_single_content(form);
        auto index = signed_index_from_buffer(buffers, form.key + "-index");
        if (index.size() != length) {
            throw std::invalid_argument("indexed array index length must match array length");
        }
        return std::make_shared<IndexedArray>(
            std::move(index), content_from_buffers(content_form, content_form.length, buffers));
    }
    case FormKind::indexed_option: {
        const auto& content_form = require_single_content(form);
        auto index = signed_index_from_buffer(buffers, form.key + "-index");
        if (index.size() != length) {
            throw std::invalid_argument("indexed-option index length must match array length");
        }
        return std::make_shared<IndexedOptionArray>(
            std::move(index), content_from_buffers(content_form, content_form.length, buffers));
    }
    case FormKind::byte_masked: {
        const auto& content_form = require_single_content(form);
        auto mask = require_buffer<std::uint8_t>(buffers, form.key + "-mask");
        if (mask.size() != length) {
            throw std::invalid_argument("byte-mask length must match array length");
        }
        return std::make_shared<ByteMaskedArray>(
            mask, content_from_buffers(content_form, content_form.length, buffers), form.valid_when);
    }
    case FormKind::bit_masked: {
        const auto& content_form = require_single_content(form);
        auto mask = require_buffer<std::uint8_t>(buffers, form.key + "-mask");
        if (mask.size() * 8 < length) {
            throw std::invalid_argument("bit-mask buffer does not contain enough bits for array length");
        }
        return std::make_shared<BitMaskedArray>(
            mask, content_from_buffers(content_form, content_form.length, buffers), length, form.valid_when,
            form.lsb_order);
    }
    case FormKind::unmasked: {
        const auto& content_form = require_single_content(form);
        return std::make_shared<UnmaskedArray>(content_from_buffers(content_form, length, buffers));
    }
    case FormKind::record: {
        if (form.fields.size() != form.contents.size()) {
            throw std::invalid_argument("record form fields and contents must have matching sizes");
        }
        std::vector<std::shared_ptr<const Content>> contents;
        contents.reserve(form.contents.size());
        for (const auto& content_form : form.contents) {
            contents.push_back(content_from_buffers(content_form, length, buffers));
        }
        return std::make_shared<RecordArray>(form.fields, std::move(contents), form.is_tuple, form.record_name, length);
    }
    case FormKind::union_: {
        auto tags = require_buffer<std::uint8_t>(buffers, form.key + "-tags");
        auto index = signed_index_from_buffer(buffers, form.key + "-index");
        if (tags.size() != length || index.size() != length) {
            throw std::invalid_argument("union tags and index lengths must match array length");
        }
        std::vector<std::shared_ptr<const Content>> contents;
        contents.reserve(form.contents.size());
        for (const auto& content_form : form.contents) {
            contents.push_back(content_from_buffers(content_form, content_form.length, buffers));
        }
        return std::make_shared<UnionArray>(std::move(tags), std::move(index), std::move(contents));
    }
    }

    throw std::invalid_argument("unsupported form kind");
}

}  // namespace detail

namespace detail {

inline Array attach_metadata(Array result, const Array& source, bool preserve_named_axes = true) {
    result = result.with_behavior(source.behavior()).with_attrs(source.attrs());
    return result.with_named_axes(preserve_named_axes ? source.named_axes() : Array::NamedAxes{});
}

inline Array attach_metadata(std::shared_ptr<const Content> layout,
                             const Array& source,
                             bool preserve_named_axes = true) {
    return attach_metadata(Array(std::move(layout)), source, preserve_named_axes);
}

template <typename Map>
Map merge_metadata_map(const Map& left, const Map& right, const char* label) {
    Map result = left;
    for (const auto& [key, value] : right) {
        const auto found = result.find(key);
        if (found != result.end() && found->second != value) {
            throw std::invalid_argument(std::string("conflicting array ") + label + ": " + key);
        }
        result[key] = value;
    }
    return result;
}

inline Array attach_merged_metadata(Array result, const Array& left, const Array& right) {
    return result
        .with_behavior(merge_metadata_map(left.behavior(), right.behavior(), "behavior"))
        .with_attrs(merge_metadata_map(left.attrs(), right.attrs(), "attrs"))
        .with_named_axes(merge_metadata_map(left.named_axes(), right.named_axes(), "named axes"));
}

inline Array remove_axis_metadata(Array result, const Array& source, int removed_axis) {
    Array::NamedAxes axes;
    for (const auto& [name, axis] : source.named_axes()) {
        auto normalized = axis;
        if (normalized < 0) normalized += static_cast<int>(source.ndim());
        if (normalized == removed_axis) continue;
        if (normalized > removed_axis) --normalized;
        axes[name] = normalized;
    }
    return result.with_behavior(source.behavior()).with_attrs(source.attrs()).with_named_axes(std::move(axes));
}

inline Array metadata_from_inputs(Array result, const std::vector<Array>& arrays) {
    if (arrays.empty()) return result;
    auto behavior = arrays.front().behavior();
    auto attrs = arrays.front().attrs();
    auto named_axes = arrays.front().named_axes();
    for (std::size_t i = 1; i < arrays.size(); ++i) {
        behavior = merge_metadata_map(behavior, arrays[i].behavior(), "behavior");
        attrs = merge_metadata_map(attrs, arrays[i].attrs(), "attrs");
        named_axes = merge_metadata_map(named_axes, arrays[i].named_axes(), "named axes");
    }
    return result.with_behavior(std::move(behavior))
        .with_attrs(std::move(attrs))
        .with_named_axes(std::move(named_axes));
}

}  // namespace detail

inline Value to_list(const Array& array) {
    return array.to_list();
}

inline ArrayType type(const Array& array) {
    return array.type();
}

inline ScalarType type(const Scalar& scalar) {
    return scalar.type();
}

inline Scalar scalar(const Value& value) {
    return Scalar(value);
}

inline Array with_attrs(const Array& array, Array::Metadata attrs) {
    return array.with_attrs(std::move(attrs));
}

inline Array with_named_axis(const Array& array, std::string name, int axis) {
    auto axes = array.named_axes();
    axes[std::move(name)] = axis;
    return array.with_named_axes(std::move(axes));
}

inline Array without_named_axis(const Array& array, const std::string& name) {
    auto axes = array.named_axes();
    axes.erase(name);
    return array.with_named_axes(std::move(axes));
}

inline std::string validity_error(const Array& array) {
    return array.validity_error();
}

inline bool is_valid(const Array& array) {
    return array.is_valid();
}

inline ToBuffersResult to_buffers(const Array& array) {
    const auto packed = array.layout().to_packed();
    detail::BufferBuilder builder;
    auto form = packed->to_buffers(builder);
    return ToBuffersResult{
        .form = std::move(form),
        .length = packed->length(),
        .buffers = std::move(builder).release(),
    };
}

inline Array from_buffers(const Form& form, std::size_t length, const BufferMap& buffers) {
    return Array(detail::content_from_buffers(form, length, buffers));
}

inline Array from_buffers(const ToBuffersResult& buffers) {
    return from_buffers(buffers.form, buffers.length, buffers.buffers);
}

inline std::vector<std::size_t> num(const Array& array) {
    return array.layout().num();
}

inline Value num(const Array& array, int axis) {
    const auto depth = static_cast<int>(array.ndim());
    if (axis < 0) axis += depth;
    if (axis < 0 || axis >= depth) throw std::invalid_argument("ak::num axis is outside the array depth");
    return detail::num_at_axis(array.to_list(), axis);
}

inline Array flatten(const Array& array) {
    return detail::remove_axis_metadata(Array(array.layout().flatten()), array, 1);
}

inline Array flatten(const Array& array, int axis) {
    const auto depth = static_cast<int>(array.ndim());
    if (axis < 0) axis += depth;
    if (axis <= 0 || axis >= depth) throw std::invalid_argument("ak::flatten axis must select a nested list");
    return detail::remove_axis_metadata(
        detail::array_from_list(detail::flatten_at_axis(array.to_list(), axis).as_list()), array, axis);
}

inline Array ravel(const Array& array) {
    Value::list_type result;
    detail::collect_ravel_values(array.to_list(), result);
    return detail::attach_metadata(detail::array_from_list(std::move(result)), array, false);
}

inline Array unflatten(const Array& array, std::span<const std::size_t> counts) {
    return detail::attach_metadata(
        Array(array.layout().unflatten(std::vector<std::size_t>(counts.begin(), counts.end()))), array);
}

inline Array unflatten(const Array& array, const std::vector<std::size_t>& counts) {
    return detail::attach_metadata(Array(array.layout().unflatten(counts)), array);
}

inline Array to_packed(const Array& array) {
    return array.with_layout(array.layout().to_packed());
}

inline Array concatenate(const std::vector<Array>& arrays, int axis = 0) {
    if (arrays.empty()) {
        return Array();
    }
    int depth = -1;
    for (const auto& array : arrays) {
        const auto current_depth = static_cast<int>(array.ndim());
        if (depth < 0) {
            depth = current_depth;
        } else if (depth != current_depth) {
            throw std::invalid_argument("ak::concatenate requires arrays with matching depth");
        }
    }
    if (axis < 0) axis += depth;
    if (axis < 0 || axis >= depth) {
        throw std::invalid_argument("ak::concatenate axis is outside the array depth");
    }
    if (axis == 0) {
        if (auto layout = kernel::concatenate_axis0(arrays)) {
            return detail::metadata_from_inputs(Array(std::move(layout)), arrays);
        }
    }
    std::vector<Value> values;
    values.reserve(arrays.size());
    for (const auto& array : arrays) values.push_back(array.to_list());
    return detail::metadata_from_inputs(
        detail::array_from_list(detail::concatenate_values(values, axis).as_list()), arrays);
}

inline Array concatenate(std::initializer_list<Array> arrays, int axis = 0) {
    return concatenate(std::vector<Array>(arrays), axis);
}

inline Array local_index(const Array& array, int axis) {
    const auto depth = static_cast<int>(array.ndim());
    if (axis < 0) axis += depth;
    if (axis < 0 || axis >= depth) {
        throw std::invalid_argument("ak::local_index axis is outside the array depth");
    }
    return detail::attach_metadata(
        detail::array_from_list(detail::local_index_at_axis(array.to_list(), axis).as_list()), array);
}

inline Array local_index(const Array& array) {
    return local_index(array, -1);
}

inline Array is_none(const Array& array, int axis = 0) {
    const auto depth = static_cast<int>(array.ndim());
    if (axis < 0) axis += depth;
    if (axis < 0 || axis >= depth) throw std::invalid_argument("ak::is_none axis is outside the array depth");
    auto values = detail::require_top_list(array);
    Value::list_type result;
    result.reserve(values.size());
    for (const auto& value : values) {
        result.push_back(detail::is_none_value(value, axis, 0));
    }
    return detail::attach_metadata(detail::array_from_list(std::move(result)), array);
}

inline Array drop_none(const Array& array, std::optional<int> axis = std::nullopt) {
    if (axis) {
        const auto depth = static_cast<int>(array.ndim());
        if (*axis < 0) *axis += depth;
        if (*axis < 0 || *axis >= depth) {
            throw std::invalid_argument("ak::drop_none axis is outside the array depth");
        }
    }
    auto values = detail::require_top_list(array);
    Value::list_type result;
    result.reserve(values.size());
    for (const auto& value : values) {
        if (!detail::should_drop_at_axis(value, axis, 0)) {
            result.push_back(detail::drop_none_value(value, axis, 0));
        }
    }
    return detail::attach_metadata(detail::array_from_list(std::move(result)), array);
}

inline Array fill_none(const Array& array, const Value& fill_value) {
    auto values = detail::require_top_list(array);
    Value::list_type result;
    result.reserve(values.size());
    for (const auto& value : values) {
        result.push_back(detail::fill_none_value(value, fill_value));
    }
    return detail::attach_metadata(detail::array_from_list(std::move(result)), array);
}

struct PadNoneOptions {
    int axis{1};
    bool clip{false};
};

inline Array pad_none(const Array& array, std::size_t target, PadNoneOptions options = {}) {
    const auto depth = static_cast<int>(array.ndim());
    if (options.axis < 0) options.axis += depth;
    if (options.axis < 0 || options.axis >= depth) {
        throw std::invalid_argument("ak::pad_none axis is outside the array depth");
    }
    const auto value = array.to_list();
    return detail::attach_metadata(
        Array(detail::layout_from_list(detail::pad_none_value(value, target, options.axis, options.clip, 0).as_list())),
        array);
}

inline Array nan_to_none(const Array& array) {
    const auto value = detail::nan_to_none_value(array.to_list());
    return detail::attach_metadata(Array(detail::layout_from_list(value.as_list())), array);
}

struct NanToNumOptions {
    double nan{0.0};
    double posinf{std::numeric_limits<double>::max()};
    double neginf{std::numeric_limits<double>::lowest()};
};

inline Array nan_to_num(const Array& array, NanToNumOptions options = {}) {
    const auto value = detail::nan_to_num_value(array.to_list(), options.nan, options.posinf, options.neginf);
    return detail::attach_metadata(Array(detail::layout_from_list(value.as_list())), array);
}

inline Array mask(const Array& array, const Array& mask, bool valid_when = true) {
    if (const auto* flat_mask = mask.layout().flat_bool_mask()) {
        if (flat_mask->size() != array.length()) {
            throw std::invalid_argument("ak::mask requires mask length to match array length");
        }
        std::vector<std::uint8_t> bytes;
        bytes.reserve(flat_mask->size());
        for (const auto item : *flat_mask) {
            bytes.push_back(item ? 1U : 0U);
        }
        return detail::attach_merged_metadata(
            Array(std::make_shared<ByteMaskedArray>(std::move(bytes), array.layout_ptr(), valid_when)), array, mask);
    }
    const auto value = detail::mask_value(array.to_list(), mask.to_list(), valid_when);
    return detail::attach_merged_metadata(Array(detail::layout_from_list(value.as_list())), array, mask);
}

inline Array firsts(const Array& array, int axis = 1) {
    const auto depth = static_cast<int>(array.ndim());
    if (axis < 0) axis += depth;
    if (axis <= 0 || axis >= depth) throw std::invalid_argument("ak::firsts axis is outside a list depth");
    const auto value = detail::firsts_value(array.to_list(), axis, 0);
    return detail::remove_axis_metadata(Array(detail::layout_from_list(value.as_list())), array, axis);
}

inline Array singletons(const Array& array) {
    return detail::attach_metadata(
        Array(detail::layout_from_list(detail::singletons_value(array.to_list()).as_list())), array);
}

struct ReducerOptions {
    std::optional<int> axis{-1};
    bool keepdims{false};
    bool mask_identity{false};
    std::optional<Value> initial{std::nullopt};
};

struct StatisticOptions {
    std::optional<int> axis{-1};
    bool keepdims{false};
    bool mask_identity{false};
    double ddof{0.0};
};

struct SortOptions {
    int axis{-1};
    bool ascending{true};
};

inline Value reduce(const Array& array,
                    detail::ReducerKind kind,
                    ReducerOptions options = {},
                    bool skip_nan = false,
                    double ddof = 0.0,
                    int moment_order = 2) {
    return detail::reduce_array_value(
        array.to_list(), options.axis, options.keepdims,
        detail::ReduceSettings{
            .kind = kind,
            .mask_identity = options.mask_identity,
            .initial = options.initial,
            .skip_nan = skip_nan,
            .ddof = ddof,
            .moment_order = moment_order,
        });
}

inline ReducerResult reducer_result(Value value,
                                    const Array& source,
                                    std::optional<int> axis,
                                    bool keepdims) {
    if (!std::holds_alternative<Value::list_type>(value.storage())) return Scalar(std::move(value));
    auto result = detail::array_from_list(std::move(value.as_list()));
    if (axis && !keepdims) {
        auto normalized = *axis;
        if (normalized < 0) normalized += static_cast<int>(source.ndim());
        result = detail::remove_axis_metadata(std::move(result), source, normalized);
    } else {
        result = detail::attach_metadata(std::move(result), source);
    }
    return result;
}

inline ReducerResult reduce_result(const Array& array,
                                   detail::ReducerKind kind,
                                   ReducerOptions options = {},
                                   bool skip_nan = false,
                                   double ddof = 0.0,
                                   int moment_order = 2) {
    const auto axis = options.axis;
    const auto keepdims = options.keepdims;
    return reducer_result(
        reduce(array, kind, std::move(options), skip_nan, ddof, moment_order), array, axis, keepdims);
}

inline Value count(const Array& array, ReducerOptions options = {}) {
    return reduce(array, detail::ReducerKind::count, std::move(options));
}

inline Value count_nonzero(const Array& array, ReducerOptions options = {}) {
    return reduce(array, detail::ReducerKind::count_nonzero, std::move(options));
}

inline Value sum(const Array& array, ReducerOptions options = {}) {
    return reduce(array, detail::ReducerKind::sum, std::move(options));
}

inline ReducerResult sum_result(const Array& array, ReducerOptions options = {}) {
    return reduce_result(array, detail::ReducerKind::sum, std::move(options));
}

inline Value prod(const Array& array, ReducerOptions options = {}) {
    return reduce(array, detail::ReducerKind::prod, std::move(options));
}

inline Value any(const Array& array, ReducerOptions options = {}) {
    return reduce(array, detail::ReducerKind::any, std::move(options));
}

inline Value all(const Array& array, ReducerOptions options = {}) {
    return reduce(array, detail::ReducerKind::all, std::move(options));
}

inline Value min(const Array& array, ReducerOptions options = {}) {
    return reduce(array, detail::ReducerKind::min, std::move(options));
}

inline Value max(const Array& array, ReducerOptions options = {}) {
    return reduce(array, detail::ReducerKind::max, std::move(options));
}

inline Value argmin(const Array& array, ReducerOptions options = {}) {
    return reduce(array, detail::ReducerKind::argmin, std::move(options));
}

inline Value argmax(const Array& array, ReducerOptions options = {}) {
    return reduce(array, detail::ReducerKind::argmax, std::move(options));
}

inline Value mean(const Array& array, StatisticOptions options = {}) {
    return reduce(
        array, detail::ReducerKind::mean,
        ReducerOptions{
            .axis = options.axis,
            .keepdims = options.keepdims,
            .mask_identity = options.mask_identity,
        });
}

inline ReducerResult mean_result(const Array& array, StatisticOptions options = {}) {
    const auto axis = options.axis;
    const auto keepdims = options.keepdims;
    return reducer_result(mean(array, options), array, axis, keepdims);
}

inline Value moment(const Array& array, int order, StatisticOptions options = {}) {
    if (order < 0) {
        throw std::invalid_argument("ak::moment order must be non-negative");
    }
    return reduce(
        array, detail::ReducerKind::moment,
        ReducerOptions{
            .axis = options.axis,
            .keepdims = options.keepdims,
            .mask_identity = options.mask_identity,
        },
        false, options.ddof, order);
}

inline Value var(const Array& array, StatisticOptions options = {}) {
    return reduce(
        array, detail::ReducerKind::var,
        ReducerOptions{
            .axis = options.axis,
            .keepdims = options.keepdims,
            .mask_identity = options.mask_identity,
        },
        false, options.ddof);
}

inline Value std(const Array& array, StatisticOptions options = {}) {
    return reduce(
        array, detail::ReducerKind::stddev,
        ReducerOptions{
            .axis = options.axis,
            .keepdims = options.keepdims,
            .mask_identity = options.mask_identity,
        },
        false, options.ddof);
}

inline Value ptp(const Array& array, StatisticOptions options = {}) {
    return reduce(
        array, detail::ReducerKind::ptp,
        ReducerOptions{
            .axis = options.axis,
            .keepdims = options.keepdims,
            .mask_identity = options.mask_identity,
        });
}

inline Value nansum(const Array& array, ReducerOptions options = {}) {
    return reduce(array, detail::ReducerKind::sum, std::move(options), true);
}

inline Value nanprod(const Array& array, ReducerOptions options = {}) {
    return reduce(array, detail::ReducerKind::prod, std::move(options), true);
}

inline Value nanmin(const Array& array, ReducerOptions options = {}) {
    return reduce(array, detail::ReducerKind::min, std::move(options), true);
}

inline Value nanmax(const Array& array, ReducerOptions options = {}) {
    return reduce(array, detail::ReducerKind::max, std::move(options), true);
}

inline Value nanmean(const Array& array, StatisticOptions options = {}) {
    return reduce(
        array, detail::ReducerKind::mean,
        ReducerOptions{
            .axis = options.axis,
            .keepdims = options.keepdims,
            .mask_identity = options.mask_identity,
        },
        true);
}

inline Value nanvar(const Array& array, StatisticOptions options = {}) {
    return reduce(
        array, detail::ReducerKind::var,
        ReducerOptions{
            .axis = options.axis,
            .keepdims = options.keepdims,
            .mask_identity = options.mask_identity,
        },
        true, options.ddof);
}

inline Value nanstd(const Array& array, StatisticOptions options = {}) {
    return reduce(
        array, detail::ReducerKind::stddev,
        ReducerOptions{
            .axis = options.axis,
            .keepdims = options.keepdims,
            .mask_identity = options.mask_identity,
        },
        true, options.ddof);
}

inline Value nanargmin(const Array& array, ReducerOptions options = {}) {
    return reduce(array, detail::ReducerKind::argmin, std::move(options), true);
}

inline Value nanargmax(const Array& array, ReducerOptions options = {}) {
    return reduce(array, detail::ReducerKind::argmax, std::move(options), true);
}

inline Array sort(const Array& array, SortOptions options = {}) {
    return detail::attach_metadata(
        Array(detail::layout_from_list(detail::sort_value(array.to_list(), options.axis, options.ascending, false).as_list())),
        array);
}

inline Array argsort(const Array& array, SortOptions options = {}) {
    return detail::attach_metadata(
        Array(detail::layout_from_list(detail::sort_value(array.to_list(), options.axis, options.ascending, true).as_list())),
        array);
}

inline Array softmax(const Array& array, int axis = -1) {
    return detail::attach_metadata(
        Array(detail::layout_from_list(detail::softmax_value(array.to_list(), axis).as_list())), array);
}

inline std::vector<std::string> fields(const Array& array) {
    return array.fields();
}

inline Array field(const Array& array, const std::string& name) {
    return array.field(name);
}

inline Array project_fields(const Array& array, const std::vector<std::string>& names) {
    return array.project_fields(names);
}

struct ZipOptions {
    std::optional<int> depth_limit{std::nullopt};
};

inline Array zip(const std::vector<std::pair<std::string, Array>>& fields, ZipOptions options = {}) {
    if (fields.empty()) {
        throw std::invalid_argument("ak::zip requires at least one field");
    }

    const auto length = fields.front().second.length();
    std::vector<std::string> names;
    std::vector<Value::list_type> values_by_field;
    names.reserve(fields.size());
    values_by_field.reserve(fields.size());

    for (const auto& [name, array] : fields) {
        if (name.empty()) {
            throw std::invalid_argument("ak::zip field names must not be empty");
        }
        if (array.length() != length) {
            throw std::invalid_argument("ak::zip field arrays must have equal lengths");
        }
        names.push_back(name);
        values_by_field.push_back(detail::require_top_list(array));
    }

    Value::list_type rows;
    rows.reserve(length);
    for (std::size_t row = 0; row < length; ++row) {
        std::vector<Value> row_values;
        row_values.reserve(values_by_field.size());
        for (const auto& field_values : values_by_field) {
            row_values.push_back(field_values[row]);
        }
        rows.push_back(detail::zip_values(names, row_values, false, options.depth_limit, 1));
    }
    std::vector<Array> inputs;
    inputs.reserve(fields.size());
    for (const auto& field : fields) inputs.push_back(field.second);
    return detail::metadata_from_inputs(detail::array_from_list(std::move(rows)), inputs);
}

inline Array zip(std::initializer_list<std::pair<std::string, Array>> fields, ZipOptions options = {}) {
    return zip(std::vector<std::pair<std::string, Array>>(fields.begin(), fields.end()), options);
}

inline Array zip(const std::vector<Array>& arrays, ZipOptions options = {}) {
    if (arrays.empty()) {
        throw std::invalid_argument("ak::zip tuple input requires at least one array");
    }
    const auto length = arrays.front().length();
    std::vector<std::string> names;
    std::vector<Value::list_type> values_by_field;
    names.reserve(arrays.size());
    values_by_field.reserve(arrays.size());
    for (std::size_t i = 0; i < arrays.size(); ++i) {
        if (arrays[i].length() != length) {
            throw std::invalid_argument("ak::zip tuple arrays must have equal lengths");
        }
        names.push_back(std::to_string(i));
        values_by_field.push_back(detail::require_top_list(arrays[i]));
    }

    Value::list_type rows;
    rows.reserve(length);
    for (std::size_t row = 0; row < length; ++row) {
        std::vector<Value> row_values;
        row_values.reserve(values_by_field.size());
        for (const auto& field_values : values_by_field) {
            row_values.push_back(field_values[row]);
        }
        rows.push_back(detail::zip_values(names, row_values, true, options.depth_limit, 1));
    }
    return detail::metadata_from_inputs(detail::array_from_list(std::move(rows)), arrays);
}

inline Array zip_no_broadcast(const std::vector<std::pair<std::string, Array>>& fields) {
    return zip(fields, {.depth_limit = 1});
}

inline Array zip_no_broadcast(std::initializer_list<std::pair<std::string, Array>> fields) {
    return zip_no_broadcast(std::vector<std::pair<std::string, Array>>(fields.begin(), fields.end()));
}

inline std::vector<Array> unzip(const Array& array) {
    const auto names = array.fields();
    if (names.empty()) {
        throw std::invalid_argument("ak::unzip requires a record or tuple array");
    }
    std::vector<Array> result;
    result.reserve(names.size());
    for (const auto& name : names) {
        result.push_back(array.field(name));
    }
    return result;
}

inline Array with_field(const Array& array, const Array& what, const std::string& name) {
    if (array.length() != what.length()) {
        throw std::invalid_argument("ak::with_field requires arrays with equal outer lengths");
    }
    const auto base = detail::require_top_list(array);
    const auto values = detail::require_top_list(what);
    Value::list_type result;
    result.reserve(base.size());
    for (std::size_t i = 0; i < base.size(); ++i) {
        result.push_back(detail::with_field_value(base[i], values[i], name));
    }
    return detail::attach_merged_metadata(detail::array_from_list(std::move(result)), array, what);
}

inline Array without_field(const Array& array, const std::string& name) {
    const auto base = detail::require_top_list(array);
    Value::list_type result;
    result.reserve(base.size());
    for (const auto& item : base) {
        result.push_back(detail::without_field_value(item, name));
    }
    return detail::attach_metadata(detail::array_from_list(std::move(result)), array);
}

inline Array with_name(const Array& array, std::string name) {
    return array.with_layout(array.layout().with_name(std::move(name)));
}

inline std::vector<Array> broadcast_arrays(const std::vector<Array>& arrays) {
    if (arrays.empty()) {
        throw std::invalid_argument("ak::broadcast_arrays requires at least one input");
    }
    std::vector<Value> values;
    values.reserve(arrays.size());
    for (const auto& array : arrays) {
        values.push_back(array.to_list());
    }

    const auto broadcasted = detail::broadcast_value_list(values);
    std::vector<Array> result;
    result.reserve(broadcasted.size());
    for (const auto& value : broadcasted) {
        if (detail::value_tag(value) != detail::ValueTag::list) {
            throw std::invalid_argument("ak::broadcast_arrays produced a non-array result");
        }
        result.push_back(detail::array_from_list(value.as_list()));
    }
    for (std::size_t i = 0; i < result.size(); ++i) {
        result[i] = detail::attach_metadata(std::move(result[i]), arrays[i]);
    }
    return result;
}

inline std::pair<Array, Array> broadcast_arrays(const Array& left, const Array& right) {
    auto result = broadcast_arrays(std::vector<Array>{left, right});
    return {std::move(result[0]), std::move(result[1])};
}

inline std::pair<Array, Array> broadcast_arrays(const Array& array, const Value& scalar) {
    const auto broadcasted = detail::broadcast_value_list({array.to_list(), scalar});
    return {detail::attach_metadata(Array(detail::layout_from_list(broadcasted[0].as_list())), array),
            detail::attach_metadata(Array(detail::layout_from_list(broadcasted[1].as_list())), array)};
}

inline std::pair<Array, Array> broadcast_arrays(const Value& scalar, const Array& array) {
    const auto broadcasted = detail::broadcast_value_list({scalar, array.to_list()});
    return {detail::attach_metadata(Array(detail::layout_from_list(broadcasted[0].as_list())), array),
            detail::attach_metadata(Array(detail::layout_from_list(broadcasted[1].as_list())), array)};
}

inline std::pair<Array, Array> broadcast_fields(const Array& left, const Array& right) {
    const auto result = detail::broadcast_fields_value(left.to_list(), right.to_list());
    return {detail::attach_merged_metadata(Array(detail::layout_from_list(result.first.as_list())), left, right),
            detail::attach_merged_metadata(Array(detail::layout_from_list(result.second.as_list())), left, right)};
}

inline Array elementwise_binary(const Array& left, const Array& right, detail::BinaryOpKind kind) {
    if (left.length() == 0 && right.length() == 0 && left.ndim() == right.ndim()) {
        return detail::attach_merged_metadata(left.with_layout(left.layout_ptr()), left, right);
    }
    if (auto layout = kernel::binary(left.layout(), right.layout(), detail::kernel_operation(kind))) {
        return detail::attach_merged_metadata(Array(std::move(layout)), left, right);
    }
    return detail::attach_merged_metadata(
        Array(detail::layout_from_list(detail::binary_value(left.to_list(), right.to_list(), kind).as_list())),
        left, right);
}

inline Array elementwise_binary(const Array& left, const Value& right, detail::BinaryOpKind kind) {
    if (left.length() == 0) return left.with_layout(left.layout_ptr());
    if (auto layout = kernel::binary(left.layout(), right, detail::kernel_operation(kind))) {
        return detail::attach_metadata(Array(std::move(layout)), left);
    }
    return detail::attach_metadata(
        Array(detail::layout_from_list(detail::binary_value(left.to_list(), right, kind).as_list())), left);
}

inline Array elementwise_binary(const Value& left, const Array& right, detail::BinaryOpKind kind) {
    if (right.length() == 0) return right.with_layout(right.layout_ptr());
    if (auto layout = kernel::binary(right.layout(), left, detail::kernel_operation(kind), true)) {
        return detail::attach_metadata(Array(std::move(layout)), right);
    }
    return detail::attach_metadata(
        Array(detail::layout_from_list(detail::binary_value(left, right.to_list(), kind).as_list())), right);
}

inline Array add(const Array& left, const Array& right) {
    return elementwise_binary(left, right, detail::BinaryOpKind::add);
}

inline Array add(const Array& left, const Value& right) {
    return elementwise_binary(left, right, detail::BinaryOpKind::add);
}

inline Array add(const Value& left, const Array& right) {
    return elementwise_binary(left, right, detail::BinaryOpKind::add);
}

inline Array subtract(const Array& left, const Array& right) {
    return elementwise_binary(left, right, detail::BinaryOpKind::subtract);
}

inline Array subtract(const Array& left, const Value& right) {
    return elementwise_binary(left, right, detail::BinaryOpKind::subtract);
}

inline Array subtract(const Value& left, const Array& right) {
    return elementwise_binary(left, right, detail::BinaryOpKind::subtract);
}

inline Array multiply(const Array& left, const Array& right) {
    return elementwise_binary(left, right, detail::BinaryOpKind::multiply);
}

inline Array multiply(const Array& left, const Value& right) {
    return elementwise_binary(left, right, detail::BinaryOpKind::multiply);
}

inline Array multiply(const Value& left, const Array& right) {
    return elementwise_binary(left, right, detail::BinaryOpKind::multiply);
}

inline Array divide(const Array& left, const Array& right) {
    return elementwise_binary(left, right, detail::BinaryOpKind::divide);
}

inline Array divide(const Array& left, const Value& right) {
    return elementwise_binary(left, right, detail::BinaryOpKind::divide);
}

inline Array divide(const Value& left, const Array& right) {
    return elementwise_binary(left, right, detail::BinaryOpKind::divide);
}

inline Array equal(const Array& left, const Array& right) {
    return elementwise_binary(left, right, detail::BinaryOpKind::equal);
}

inline Array equal(const Array& left, const Value& right) {
    return elementwise_binary(left, right, detail::BinaryOpKind::equal);
}

inline Array equal(const Value& left, const Array& right) {
    return elementwise_binary(left, right, detail::BinaryOpKind::equal);
}

inline Array not_equal(const Array& left, const Array& right) {
    return elementwise_binary(left, right, detail::BinaryOpKind::not_equal);
}

inline Array not_equal(const Array& left, const Value& right) {
    return elementwise_binary(left, right, detail::BinaryOpKind::not_equal);
}

inline Array not_equal(const Value& left, const Array& right) {
    return elementwise_binary(left, right, detail::BinaryOpKind::not_equal);
}

inline Array less(const Array& left, const Array& right) {
    return elementwise_binary(left, right, detail::BinaryOpKind::less);
}

inline Array less(const Array& left, const Value& right) {
    return elementwise_binary(left, right, detail::BinaryOpKind::less);
}

inline Array less(const Value& left, const Array& right) {
    return elementwise_binary(left, right, detail::BinaryOpKind::less);
}

inline Array less_equal(const Array& left, const Array& right) {
    return elementwise_binary(left, right, detail::BinaryOpKind::less_equal);
}

inline Array less_equal(const Array& left, const Value& right) {
    return elementwise_binary(left, right, detail::BinaryOpKind::less_equal);
}

inline Array less_equal(const Value& left, const Array& right) {
    return elementwise_binary(left, right, detail::BinaryOpKind::less_equal);
}

inline Array greater(const Array& left, const Array& right) {
    return elementwise_binary(left, right, detail::BinaryOpKind::greater);
}

inline Array greater(const Array& left, const Value& right) {
    return elementwise_binary(left, right, detail::BinaryOpKind::greater);
}

inline Array greater(const Value& left, const Array& right) {
    return elementwise_binary(left, right, detail::BinaryOpKind::greater);
}

inline Array greater_equal(const Array& left, const Array& right) {
    return elementwise_binary(left, right, detail::BinaryOpKind::greater_equal);
}

inline Array greater_equal(const Array& left, const Value& right) {
    return elementwise_binary(left, right, detail::BinaryOpKind::greater_equal);
}

inline Array greater_equal(const Value& left, const Array& right) {
    return elementwise_binary(left, right, detail::BinaryOpKind::greater_equal);
}

inline Array logical_and(const Array& left, const Array& right) {
    return elementwise_binary(left, right, detail::BinaryOpKind::logical_and);
}

inline Array logical_and(const Array& left, const Value& right) {
    return elementwise_binary(left, right, detail::BinaryOpKind::logical_and);
}

inline Array logical_and(const Value& left, const Array& right) {
    return elementwise_binary(left, right, detail::BinaryOpKind::logical_and);
}

inline Array logical_or(const Array& left, const Array& right) {
    return elementwise_binary(left, right, detail::BinaryOpKind::logical_or);
}

inline Array logical_or(const Array& left, const Value& right) {
    return elementwise_binary(left, right, detail::BinaryOpKind::logical_or);
}

inline Array logical_or(const Value& left, const Array& right) {
    return elementwise_binary(left, right, detail::BinaryOpKind::logical_or);
}

inline Array logical_not(const Array& array) {
    if (array.length() == 0) return array.with_layout(array.layout_ptr());
    if (auto layout = kernel::logical_not(array.layout())) {
        return detail::attach_metadata(Array(std::move(layout)), array);
    }
    return detail::attach_metadata(
        Array(detail::layout_from_list(detail::logical_not_value(array.to_list()).as_list())), array);
}

inline Array operator+(const Array& left, const Array& right) { return add(left, right); }
inline Array operator+(const Array& left, const Value& right) { return add(left, right); }
inline Array operator+(const Value& left, const Array& right) { return add(left, right); }
inline Array operator-(const Array& left, const Array& right) { return subtract(left, right); }
inline Array operator-(const Array& left, const Value& right) { return subtract(left, right); }
inline Array operator-(const Value& left, const Array& right) { return subtract(left, right); }
inline Array operator*(const Array& left, const Array& right) { return multiply(left, right); }
inline Array operator*(const Array& left, const Value& right) { return multiply(left, right); }
inline Array operator*(const Value& left, const Array& right) { return multiply(left, right); }
inline Array operator/(const Array& left, const Array& right) { return divide(left, right); }
inline Array operator/(const Array& left, const Value& right) { return divide(left, right); }
inline Array operator/(const Value& left, const Array& right) { return divide(left, right); }
inline Array operator==(const Array& left, const Array& right) { return equal(left, right); }
inline Array operator==(const Array& left, const Value& right) { return equal(left, right); }
inline Array operator==(const Value& left, const Array& right) { return equal(left, right); }
inline Array operator!=(const Array& left, const Array& right) { return not_equal(left, right); }
inline Array operator!=(const Array& left, const Value& right) { return not_equal(left, right); }
inline Array operator!=(const Value& left, const Array& right) { return not_equal(left, right); }
inline Array operator<(const Array& left, const Array& right) { return less(left, right); }
inline Array operator<(const Array& left, const Value& right) { return less(left, right); }
inline Array operator<(const Value& left, const Array& right) { return less(left, right); }
inline Array operator<=(const Array& left, const Array& right) { return less_equal(left, right); }
inline Array operator<=(const Array& left, const Value& right) { return less_equal(left, right); }
inline Array operator<=(const Value& left, const Array& right) { return less_equal(left, right); }
inline Array operator>(const Array& left, const Array& right) { return greater(left, right); }
inline Array operator>(const Array& left, const Value& right) { return greater(left, right); }
inline Array operator>(const Value& left, const Array& right) { return greater(left, right); }
inline Array operator>=(const Array& left, const Array& right) { return greater_equal(left, right); }
inline Array operator>=(const Array& left, const Value& right) { return greater_equal(left, right); }
inline Array operator>=(const Value& left, const Array& right) { return greater_equal(left, right); }
inline Array operator&(const Array& left, const Array& right) { return logical_and(left, right); }
inline Array operator&(const Array& left, const Value& right) { return logical_and(left, right); }
inline Array operator&(const Value& left, const Array& right) { return logical_and(left, right); }
inline Array operator|(const Array& left, const Array& right) { return logical_or(left, right); }
inline Array operator|(const Array& left, const Value& right) { return logical_or(left, right); }
inline Array operator|(const Value& left, const Array& right) { return logical_or(left, right); }
inline Array operator!(const Array& array) { return logical_not(array); }

inline Array where(const Array& condition, const Array& left, const Array& right) {
    return detail::metadata_from_inputs(
        Array(detail::layout_from_list(detail::where_value(condition.to_list(), left.to_list(), right.to_list()).as_list())),
        {condition, left, right});
}

inline Array where(const Array& condition, const Array& left, const Value& right) {
    return detail::metadata_from_inputs(
        Array(detail::layout_from_list(detail::where_value(condition.to_list(), left.to_list(), right).as_list())),
        {condition, left});
}

inline Array where(const Array& condition, const Value& left, const Array& right) {
    return detail::metadata_from_inputs(
        Array(detail::layout_from_list(detail::where_value(condition.to_list(), left, right.to_list()).as_list())),
        {condition, right});
}

inline Array where(const Array& condition, const Value& left, const Value& right) {
    return detail::attach_metadata(
        Array(detail::layout_from_list(detail::where_value(condition.to_list(), left, right).as_list())), condition);
}

struct CloseOptions {
    double rtol{1.0e-5};
    double atol{1.0e-8};
    bool equal_nan{false};
};

inline Array isclose(const Array& left, const Array& right, CloseOptions options = {}) {
    return detail::attach_merged_metadata(
        Array(detail::layout_from_list(
            detail::isclose_value(left.to_list(), right.to_list(), options.rtol, options.atol, options.equal_nan).as_list())),
        left, right);
}

inline Array isclose(const Array& left, const Value& right, CloseOptions options = {}) {
    return detail::attach_metadata(
        Array(detail::layout_from_list(
            detail::isclose_value(left.to_list(), right, options.rtol, options.atol, options.equal_nan).as_list())),
        left);
}

inline Array isclose(const Value& left, const Array& right, CloseOptions options = {}) {
    return detail::attach_metadata(
        Array(detail::layout_from_list(
            detail::isclose_value(left, right.to_list(), options.rtol, options.atol, options.equal_nan).as_list())),
        right);
}

inline bool array_equal(const Array& left, const Array& right, bool equal_nan = false) {
    return detail::equal_value(left.to_list(), right.to_list(), equal_nan);
}

inline bool almost_equal(const Array& left, const Array& right, CloseOptions options = {}) {
    return detail::almost_equal_value(left.to_list(), right.to_list(), options.rtol, options.atol, options.equal_nan);
}

inline Array zeros_like(const Array& array) {
    return detail::attach_metadata(
        Array(detail::layout_from_list(detail::zeros_like_value(array.to_list()).as_list())), array);
}

inline Array ones_like(const Array& array) {
    return detail::attach_metadata(
        Array(detail::layout_from_list(detail::ones_like_value(array.to_list()).as_list())), array);
}

inline Array full_like(const Array& array, const Value& fill_value) {
    return detail::attach_metadata(
        Array(detail::layout_from_list(detail::like_value(array.to_list(), fill_value).as_list())), array);
}

template <typename T>
Array from_iter(const std::vector<T>& values) {
    using Storage = detail::storage_type_t<T>;
    std::vector<Storage> normalized;
    normalized.reserve(values.size());
    for (const auto& value : values) {
        normalized.push_back(detail::normalize_value(value));
    }
    if constexpr (detail::is_string_like_v<T>) {
        return Array(std::make_shared<StringArray>(normalized));
    } else {
        return Array(std::make_shared<NumpyArray<Storage>>(std::move(normalized)));
    }
}

template <typename T>
Array from_iter(std::initializer_list<T> values) {
    using Storage = detail::storage_type_t<T>;
    std::vector<Storage> normalized;
    normalized.reserve(values.size());
    for (const auto& value : values) {
        normalized.push_back(detail::normalize_value(value));
    }
    if constexpr (detail::is_string_like_v<T>) {
        return Array(std::make_shared<StringArray>(normalized));
    } else {
        return Array(std::make_shared<NumpyArray<Storage>>(std::move(normalized)));
    }
}

template <typename T>
Array from_iter(const std::vector<std::optional<T>>& values) {
    Value::list_type list;
    list.reserve(values.size());
    for (const auto& value : values) {
        if (value) {
            list.emplace_back(detail::normalize_value(*value));
        } else {
            list.emplace_back(nullptr);
        }
    }
    return detail::array_from_list(std::move(list));
}

template <typename T>
Array from_iter(std::initializer_list<Option<T>> values) {
    Value::list_type list;
    list.reserve(values.size());
    for (const auto& value : values) {
        if (value.has_value()) {
            list.emplace_back(detail::normalize_value(value.value()));
        } else {
            list.emplace_back(nullptr);
        }
    }
    return detail::array_from_list(std::move(list));
}

template <typename T>
Array from_iter(const std::vector<std::vector<T>>& rows) {
    using Storage = detail::storage_type_t<T>;
    std::vector<Storage> values;
    std::vector<std::size_t> offsets;
    offsets.reserve(rows.size() + 1);
    offsets.push_back(0);

    for (const auto& row : rows) {
        for (const auto& value : row) {
            values.push_back(detail::normalize_value(value));
        }
        offsets.push_back(values.size());
    }

    if constexpr (detail::is_string_like_v<T>) {
        auto content = std::make_shared<StringArray>(values);
        return Array(std::make_shared<ListOffsetContentArray>(std::move(content), std::move(offsets)));
    } else {
        return Array(std::make_shared<ListOffsetArray<Storage>>(std::move(values), std::move(offsets)));
    }
}

template <typename T>
Array from_iter(const std::vector<std::vector<std::optional<T>>>& rows) {
    Value::list_type outer;
    outer.reserve(rows.size());
    for (const auto& row : rows) {
        Value::list_type inner;
        inner.reserve(row.size());
        for (const auto& value : row) {
            if (value) {
                inner.emplace_back(detail::normalize_value(*value));
            } else {
                inner.emplace_back(nullptr);
            }
        }
        outer.emplace_back(std::move(inner));
    }
    return detail::array_from_list(std::move(outer));
}

template <typename T>
Array from_iter(std::initializer_list<std::initializer_list<Option<T>>> rows) {
    Value::list_type outer;
    outer.reserve(rows.size());
    for (const auto& row : rows) {
        Value::list_type inner;
        inner.reserve(row.size());
        for (const auto& value : row) {
            if (value.has_value()) {
                inner.emplace_back(detail::normalize_value(value.value()));
            } else {
                inner.emplace_back(nullptr);
            }
        }
        outer.emplace_back(std::move(inner));
    }
    return detail::array_from_list(std::move(outer));
}

template <typename T>
Array from_iter(std::initializer_list<std::initializer_list<T>> rows) {
    using Storage = detail::storage_type_t<T>;
    std::vector<Storage> values;
    std::vector<std::size_t> offsets;
    offsets.reserve(rows.size() + 1);
    offsets.push_back(0);

    for (const auto& row : rows) {
        for (const auto& value : row) {
            values.push_back(detail::normalize_value(value));
        }
        offsets.push_back(values.size());
    }

    if constexpr (detail::is_string_like_v<T>) {
        auto content = std::make_shared<StringArray>(values);
        return Array(std::make_shared<ListOffsetContentArray>(std::move(content), std::move(offsets)));
    } else {
        return Array(std::make_shared<ListOffsetArray<Storage>>(std::move(values), std::move(offsets)));
    }
}

template <typename T>
Array regular(std::vector<T> values, std::size_t size) {
    using Storage = detail::storage_type_t<T>;
    std::vector<Storage> normalized;
    normalized.reserve(values.size());
    for (const auto& value : values) {
        normalized.push_back(detail::normalize_value(value));
    }
    return Array(std::make_shared<RegularArray<Storage>>(std::move(normalized), size));
}

}  // namespace ak

namespace ak::str {

struct SplitOptions {
    std::optional<std::size_t> max_splits;
};

namespace detail {

inline bool ascii_alpha(unsigned char value) noexcept {
    return (value >= 'A' && value <= 'Z') || (value >= 'a' && value <= 'z');
}

inline bool ascii_upper(unsigned char value) noexcept {
    return value >= 'A' && value <= 'Z';
}

inline bool ascii_lower(unsigned char value) noexcept {
    return value >= 'a' && value <= 'z';
}

inline bool ascii_digit(unsigned char value) noexcept {
    return value >= '0' && value <= '9';
}

inline bool ascii_space(unsigned char value) noexcept {
    return value == ' ' || value == '\t' || value == '\n' || value == '\r' || value == '\f' || value == '\v';
}

inline unsigned char ascii_to_lower(unsigned char value) noexcept {
    return ascii_upper(value) ? static_cast<unsigned char>(value + ('a' - 'A')) : value;
}

inline unsigned char ascii_to_upper(unsigned char value) noexcept {
    return ascii_lower(value) ? static_cast<unsigned char>(value - ('a' - 'A')) : value;
}

inline bool all_ascii(std::string_view value) noexcept {
    return std::all_of(value.begin(), value.end(), [](unsigned char item) { return item <= 0x7f; });
}

template <typename Operation>
Value map_strings(const Value& value, std::string_view operation_name, const Operation& operation) {
    if (value.is_none()) {
        return Value(nullptr);
    }
    const auto tag = ak::detail::value_tag(value);
    if (tag == ak::detail::ValueTag::string) {
        return operation(std::get<std::string>(value.storage()));
    }
    if (tag == ak::detail::ValueTag::list) {
        Value::list_type result;
        result.reserve(value.as_list().size());
        for (const auto& item : value.as_list()) {
            result.push_back(map_strings(item, operation_name, operation));
        }
        return Value(std::move(result));
    }
    if (tag == ak::detail::ValueTag::record) {
        const auto& source = value.as_record();
        Value::record_type result;
        result.fields = source.fields;
        result.is_tuple = source.is_tuple;
        result.values.reserve(source.values.size());
        for (const auto& item : source.values) {
            result.values.push_back(map_strings(item, operation_name, operation));
        }
        return Value(std::move(result));
    }
    throw std::invalid_argument("ak::str::" + std::string(operation_name) + " requires string values");
}

template <typename Operation>
Array apply(const Array& array, std::string_view operation_name, const Operation& operation) {
    auto result = map_strings(array.to_list(), operation_name, operation);
    return ak::detail::attach_metadata(ak::detail::array_from_list(std::move(result.as_list())), array);
}

template <typename Predicate>
Array predicate(const Array& array, std::string_view name, const Predicate& predicate) {
    return apply(array, name, [&predicate](const std::string& value) { return Value(predicate(value)); });
}

template <typename Transform>
Array transform(const Array& array, std::string_view name, const Transform& transform) {
    return apply(array, name, [&transform](const std::string& value) { return Value(transform(value)); });
}

inline std::string join_strings(const Value::list_type& values, std::string_view separator) {
    std::string result;
    bool first = true;
    for (const auto& value : values) {
        if (value.is_none()) {
            throw std::invalid_argument("missing string");
        }
        if (ak::detail::value_tag(value) != ak::detail::ValueTag::string) {
            throw std::invalid_argument("ak::str::join requires lists containing only strings");
        }
        if (!first) {
            result.append(separator);
        }
        result.append(std::get<std::string>(value.storage()));
        first = false;
    }
    return result;
}

inline Value join_value(const Value& value, std::string_view separator) {
    if (value.is_none()) {
        return Value(nullptr);
    }
    if (ak::detail::value_tag(value) == ak::detail::ValueTag::record) {
        const auto& source = value.as_record();
        Value::record_type result;
        result.fields = source.fields;
        result.is_tuple = source.is_tuple;
        result.values.reserve(source.values.size());
        for (const auto& item : source.values) {
            result.values.push_back(join_value(item, separator));
        }
        return Value(std::move(result));
    }
    if (ak::detail::value_tag(value) != ak::detail::ValueTag::list) {
        throw std::invalid_argument("ak::str::join requires an array of string lists");
    }

    const auto& values = value.as_list();
    const bool is_string_list = std::all_of(values.begin(), values.end(), [](const Value& item) {
        return item.is_none() || ak::detail::value_tag(item) == ak::detail::ValueTag::string;
    });
    if (is_string_list) {
        if (std::any_of(values.begin(), values.end(), [](const Value& item) { return item.is_none(); })) {
            return Value(nullptr);
        }
        return Value(join_strings(values, separator));
    }

    Value::list_type result;
    result.reserve(values.size());
    for (const auto& item : values) {
        result.push_back(join_value(item, separator));
    }
    return Value(std::move(result));
}

}  // namespace detail

inline Array is_alpha(const Array& array) {
    return detail::predicate(array, "is_alpha", [](const std::string& value) {
        return !value.empty() && detail::all_ascii(value) &&
               std::all_of(value.begin(), value.end(), [](unsigned char item) { return detail::ascii_alpha(item); });
    });
}

inline Array is_alnum(const Array& array) {
    return detail::predicate(array, "is_alnum", [](const std::string& value) {
        return !value.empty() && detail::all_ascii(value) && std::all_of(value.begin(), value.end(), [](unsigned char item) {
                   return detail::ascii_alpha(item) || detail::ascii_digit(item);
               });
    });
}

inline Array is_ascii(const Array& array) {
    return detail::predicate(array, "is_ascii", [](const std::string& value) { return detail::all_ascii(value); });
}

inline Array is_decimal(const Array& array) {
    return detail::predicate(array, "is_decimal", [](const std::string& value) {
        return !value.empty() && detail::all_ascii(value) &&
               std::all_of(value.begin(), value.end(), [](unsigned char item) { return detail::ascii_digit(item); });
    });
}

inline Array is_digit(const Array& array) {
    return detail::predicate(array, "is_digit", [](const std::string& value) {
        return !value.empty() && detail::all_ascii(value) &&
               std::all_of(value.begin(), value.end(), [](unsigned char item) { return detail::ascii_digit(item); });
    });
}

inline Array is_numeric(const Array& array) {
    return detail::predicate(array, "is_numeric", [](const std::string& value) {
        return !value.empty() && detail::all_ascii(value) &&
               std::all_of(value.begin(), value.end(), [](unsigned char item) { return detail::ascii_digit(item); });
    });
}

inline Array is_space(const Array& array) {
    return detail::predicate(array, "is_space", [](const std::string& value) {
        return !value.empty() && detail::all_ascii(value) &&
               std::all_of(value.begin(), value.end(), [](unsigned char item) { return detail::ascii_space(item); });
    });
}

inline Array is_printable(const Array& array) {
    return detail::predicate(array, "is_printable", [](const std::string& value) {
        return detail::all_ascii(value) && std::all_of(value.begin(), value.end(), [](unsigned char item) {
                   return item >= 0x20 && item <= 0x7e;
               });
    });
}

inline Array is_lower(const Array& array) {
    return detail::predicate(array, "is_lower", [](const std::string& value) {
        if (!detail::all_ascii(value)) {
            return false;
        }
        bool has_lower = false;
        for (const auto item : value) {
            const auto byte = static_cast<unsigned char>(item);
            if (detail::ascii_upper(byte)) {
                return false;
            }
            has_lower = has_lower || detail::ascii_lower(byte);
        }
        return has_lower;
    });
}

inline Array is_upper(const Array& array) {
    return detail::predicate(array, "is_upper", [](const std::string& value) {
        if (!detail::all_ascii(value)) {
            return false;
        }
        bool has_upper = false;
        for (const auto item : value) {
            const auto byte = static_cast<unsigned char>(item);
            if (detail::ascii_lower(byte)) {
                return false;
            }
            has_upper = has_upper || detail::ascii_upper(byte);
        }
        return has_upper;
    });
}

inline Array is_title(const Array& array) {
    return detail::predicate(array, "is_title", [](const std::string& value) {
        if (!detail::all_ascii(value)) {
            return false;
        }
        bool expect_upper = true;
        bool has_cased = false;
        for (const auto item : value) {
            const auto byte = static_cast<unsigned char>(item);
            if (!detail::ascii_alpha(byte)) {
                expect_upper = true;
                continue;
            }
            if ((expect_upper && !detail::ascii_upper(byte)) || (!expect_upper && !detail::ascii_lower(byte))) {
                return false;
            }
            expect_upper = false;
            has_cased = true;
        }
        return has_cased;
    });
}

inline Array lower(const Array& array) {
    return detail::transform(array, "lower", [](std::string value) {
        for (auto& item : value) {
            item = static_cast<char>(detail::ascii_to_lower(static_cast<unsigned char>(item)));
        }
        return value;
    });
}

inline Array upper(const Array& array) {
    return detail::transform(array, "upper", [](std::string value) {
        for (auto& item : value) {
            item = static_cast<char>(detail::ascii_to_upper(static_cast<unsigned char>(item)));
        }
        return value;
    });
}

inline Array capitalize(const Array& array) {
    return detail::transform(array, "capitalize", [](std::string value) {
        if (!value.empty()) {
            value.front() = static_cast<char>(detail::ascii_to_upper(static_cast<unsigned char>(value.front())));
            for (std::size_t i = 1; i < value.size(); ++i) {
                value[i] = static_cast<char>(detail::ascii_to_lower(static_cast<unsigned char>(value[i])));
            }
        }
        return value;
    });
}

inline Array title(const Array& array) {
    return detail::transform(array, "title", [](std::string value) {
        bool start_of_word = true;
        for (auto& item : value) {
            const auto byte = static_cast<unsigned char>(item);
            if (detail::ascii_alpha(byte)) {
                item = static_cast<char>(start_of_word ? detail::ascii_to_upper(byte) : detail::ascii_to_lower(byte));
                start_of_word = false;
            } else {
                start_of_word = true;
            }
        }
        return value;
    });
}

inline Array reverse(const Array& array) {
    return detail::transform(array, "reverse", [](std::string value) {
        std::reverse(value.begin(), value.end());
        return value;
    });
}

inline Array slice(const Array& array,
                   std::optional<std::ptrdiff_t> start = std::nullopt,
                   std::optional<std::ptrdiff_t> stop = std::nullopt,
                   std::ptrdiff_t step = 1) {
    return detail::transform(array, "slice", [start, stop, step](const std::string& value) {
        std::string result;
        for (const auto index : index::detail::indices_for_slice(index::Slice{start, stop, step}, value.size())) {
            result.push_back(value[index]);
        }
        return result;
    });
}

inline Array split_pattern(const Array& array, std::string_view pattern, SplitOptions options = {}) {
    if (pattern.empty()) {
        throw std::invalid_argument("ak::str::split_pattern requires a non-empty pattern");
    }
    return detail::apply(array, "split_pattern", [pattern, options](const std::string& value) {
        Value::list_type pieces;
        std::size_t begin = 0;
        std::size_t splits = 0;
        while (!options.max_splits || splits < *options.max_splits) {
            const auto found = value.find(pattern, begin);
            if (found == std::string::npos) {
                break;
            }
            pieces.emplace_back(value.substr(begin, found - begin));
            begin = found + pattern.size();
            ++splits;
        }
        pieces.emplace_back(value.substr(begin));
        return Value(std::move(pieces));
    });
}

inline Array join(const Array& array, std::string_view separator) {
    const auto source = array.to_list();
    const auto& top = source.as_list();
    Value::list_type result;
    result.reserve(top.size());
    for (const auto& item : top) {
        result.push_back(detail::join_value(item, separator));
    }
    return ak::detail::attach_metadata(ak::detail::array_from_list(std::move(result)), array);
}

inline Array contains(const Array& array, std::string_view pattern) {
    return detail::predicate(array, "contains", [pattern](const std::string& value) {
        return value.find(pattern) != std::string::npos;
    });
}

inline Array starts_with(const Array& array, std::string_view pattern) {
    return detail::predicate(array, "starts_with", [pattern](const std::string& value) {
        return value.starts_with(pattern);
    });
}

inline Array ends_with(const Array& array, std::string_view pattern) {
    return detail::predicate(array, "ends_with", [pattern](const std::string& value) {
        return value.ends_with(pattern);
    });
}

}  // namespace ak::str

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
