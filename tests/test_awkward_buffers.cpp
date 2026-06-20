#include <awkward/awkward.hpp>

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace {

template <typename Exception, typename Function>
void assert_throws(Function function) {
    bool threw = false;
    try {
        function();
    } catch (const Exception&) {
        threw = true;
    }
    assert(threw);
}

template <typename T>
const std::vector<T>& buffer_as(const ak::BufferMap& buffers, const std::string& key) {
    const auto found = buffers.find(key);
    assert(found != buffers.end());
    const auto* values = std::get_if<std::vector<T>>(&found->second);
    assert(values != nullptr);
    return *values;
}

ak::Value list(std::vector<ak::Value> values) {
    return ak::Value(std::move(values));
}

ak::Value record(std::vector<std::pair<std::string, ak::Value>> values) {
    ak::Value::record_type result;
    result.fields.reserve(values.size());
    result.values.reserve(values.size());
    for (auto& [name, value] : values) {
        result.fields.push_back(std::move(name));
        result.values.push_back(std::move(value));
    }
    return result;
}

}  // namespace

int main() {
    const auto flat = ak::from_iter({1, 2, 3});
    assert(flat.nbytes() == 3 * sizeof(std::int64_t));
    const auto flat_buffers = ak::to_buffers(flat);
    assert(flat_buffers.length == 3);
    assert(flat_buffers.form.kind == ak::FormKind::numpy);
    assert(flat_buffers.form.key == "node0");
    assert(flat_buffers.form.primitive == "int64");
    assert(buffer_as<std::int64_t>(flat_buffers.buffers, "node0-data") ==
           std::vector<std::int64_t>({1, 2, 3}));
    assert(ak::to_list(ak::from_buffers(flat_buffers)) == ak::to_list(flat));

    const auto nested = ak::from_iter({{1, 2}, {}, {3}});
    assert(nested.nbytes() == 3 * sizeof(std::int64_t) + 4 * sizeof(std::size_t));
    const auto nested_buffers = ak::to_buffers(nested);
    assert(nested_buffers.form.kind == ak::FormKind::list_offset);
    assert(nested_buffers.form.key == "node0");
    assert(nested_buffers.form.contents.size() == 1);
    assert(nested_buffers.form.contents[0].kind == ak::FormKind::numpy);
    assert(nested_buffers.form.contents[0].key == "node1");
    assert(buffer_as<std::int64_t>(nested_buffers.buffers, "node0-offsets") ==
           std::vector<std::int64_t>({0, 2, 2, 3}));
    assert(buffer_as<std::int64_t>(nested_buffers.buffers, "node1-data") ==
           std::vector<std::int64_t>({1, 2, 3}));
    assert(ak::to_list(ak::from_buffers(nested_buffers)) == ak::to_list(nested));

    const auto regular = ak::regular(std::vector<double>{1.0, 2.0, 3.0, 4.0}, 2);
    const auto regular_buffers = ak::to_buffers(regular);
    assert(regular_buffers.form.kind == ak::FormKind::regular);
    assert(regular_buffers.form.size == 2);
    assert(buffer_as<double>(regular_buffers.buffers, "node1-data") ==
           std::vector<double>({1.0, 2.0, 3.0, 4.0}));
    assert(ak::to_list(ak::from_buffers(regular_buffers)) == ak::to_list(regular));

    const ak::Array zero_regular(std::make_shared<ak::RegularArray<int>>(
        std::vector<int>{}, 0, 3));
    const auto zero_regular_buffers = ak::to_buffers(zero_regular);
    assert(zero_regular_buffers.form.kind == ak::FormKind::regular);
    assert(zero_regular_buffers.form.length == 3);
    assert(zero_regular_buffers.form.size == 0);
    assert(ak::to_list(ak::from_buffers(zero_regular_buffers)) == ak::to_list(zero_regular));

    const auto indirection_content = ak::from_iter({10, 20, 30, 40});
    const ak::Array list_array(std::make_shared<ak::ListArray>(
        std::vector<std::size_t>{2, 0}, std::vector<std::size_t>{4, 1}, indirection_content.layout_ptr()));
    ak::detail::BufferBuilder list_builder;
    auto list_form = list_array.layout().to_buffers(list_builder);
    const ak::ToBuffersResult list_buffers{
        .form = std::move(list_form),
        .length = list_array.length(),
        .buffers = std::move(list_builder).release(),
    };
    assert(list_buffers.form.kind == ak::FormKind::list);
    assert(buffer_as<std::int64_t>(list_buffers.buffers, "node0-starts") ==
           std::vector<std::int64_t>({2, 0}));
    assert(buffer_as<std::int64_t>(list_buffers.buffers, "node0-stops") ==
           std::vector<std::int64_t>({4, 1}));
    assert(ak::to_list(ak::from_buffers(list_buffers)) == ak::to_list(list_array));

    const ak::Array indexed(std::make_shared<ak::IndexedArray>(
        std::vector<std::ptrdiff_t>{3, 1, 3}, indirection_content.layout_ptr()));
    ak::detail::BufferBuilder indexed_builder;
    auto indexed_form = indexed.layout().to_buffers(indexed_builder);
    const ak::ToBuffersResult indexed_buffers{
        .form = std::move(indexed_form),
        .length = indexed.length(),
        .buffers = std::move(indexed_builder).release(),
    };
    assert(indexed_buffers.form.kind == ak::FormKind::indexed);
    assert(buffer_as<std::int64_t>(indexed_buffers.buffers, "node0-index") ==
           std::vector<std::int64_t>({3, 1, 3}));
    assert(ak::to_list(ak::from_buffers(indexed_buffers)) == ak::to_list(indexed));

    const auto option = ak::from_iter<int>({1, ak::none, 3});
    const auto option_buffers = ak::to_buffers(option);
    assert(option_buffers.form.kind == ak::FormKind::indexed_option);
    assert(buffer_as<std::int64_t>(option_buffers.buffers, "node0-index") ==
           std::vector<std::int64_t>({0, -1, 1}));
    assert(buffer_as<std::int64_t>(option_buffers.buffers, "node1-data") ==
           std::vector<std::int64_t>({1, 3}));
    assert(ak::to_list(ak::from_buffers(option_buffers)) == list({1, nullptr, 3}));

    const auto sparse_content = ak::from_iter({10, 20, 30});
    ak::Array sparse_option(std::make_shared<ak::IndexedOptionArray>(
        std::vector<std::ptrdiff_t>{2, -1, 0}, sparse_content.layout_ptr()));
    const auto sparse_buffers = ak::to_buffers(sparse_option);
    assert(buffer_as<std::int64_t>(sparse_buffers.buffers, "node0-index") ==
           std::vector<std::int64_t>({0, -1, 1}));
    assert(buffer_as<std::int64_t>(sparse_buffers.buffers, "node1-data") ==
           std::vector<std::int64_t>({30, 10}));
    assert(ak::to_list(ak::from_buffers(sparse_buffers)) == list({30, nullptr, 10}));

    ak::Array byte_masked(std::make_shared<ak::ByteMaskedArray>(
        std::vector<std::uint8_t>{1, 0, 1}, sparse_content.layout_ptr(), true));
    const auto byte_buffers = ak::to_buffers(byte_masked);
    assert(byte_buffers.form.kind == ak::FormKind::byte_masked);
    assert(byte_buffers.form.valid_when);
    assert(buffer_as<std::uint8_t>(byte_buffers.buffers, "node0-mask") ==
           std::vector<std::uint8_t>({1, 0, 1}));
    assert(ak::to_list(ak::from_buffers(byte_buffers)) == list({10, nullptr, 30}));

    ak::Array bit_masked(std::make_shared<ak::BitMaskedArray>(
        std::vector<std::uint8_t>{0b00000101}, sparse_content.layout_ptr(), 3, true, true));
    const auto bit_buffers = ak::to_buffers(bit_masked);
    assert(bit_buffers.form.kind == ak::FormKind::bit_masked);
    assert(bit_buffers.form.lsb_order);
    assert(buffer_as<std::uint8_t>(bit_buffers.buffers, "node0-mask") ==
           std::vector<std::uint8_t>({0b00000101}));
    assert(ak::to_list(ak::from_buffers(bit_buffers)) == list({10, nullptr, 30}));

    const auto nested_option = ak::from_iter<int>({{1, ak::none}, {ak::none, 4}});
    const auto nested_option_buffers = ak::to_buffers(nested_option);
    assert(nested_option_buffers.form.kind == ak::FormKind::list_offset);
    assert(nested_option_buffers.form.contents[0].kind == ak::FormKind::indexed_option);
    assert(ak::to_list(ak::from_buffers(nested_option_buffers)) == ak::to_list(nested_option));

    const auto records = ak::zip(
        {{"x", ak::from_iter({1, 2})}, {"y", ak::from_iter({{10}, {20, 21}})}}, {.depth_limit = 1});
    const auto record_buffers = ak::to_buffers(records);
    assert(record_buffers.form.kind == ak::FormKind::record);
    assert(record_buffers.form.fields == std::vector<std::string>({"x", "y"}));
    assert(record_buffers.form.contents.size() == 2);
    assert(record_buffers.form.contents[1].kind == ak::FormKind::list_offset);
    assert(ak::to_list(ak::from_buffers(record_buffers)) ==
           list({record({{"x", 1}, {"y", list({10})}}), record({{"x", 2}, {"y", list({20, 21})}})}));

    const auto four_records = ak::zip(
        {{"x", ak::from_iter({1, 2, 3, 4})}, {"y", ak::from_iter({10, 20, 30, 40})}}, {.depth_limit = 1});
    const ak::Array regular_records(std::make_shared<ak::RegularContentArray>(four_records.layout_ptr(), 2));
    const auto regular_record_buffers = ak::to_buffers(regular_records);
    assert(regular_record_buffers.form.kind == ak::FormKind::regular);
    assert(regular_record_buffers.form.contents[0].kind == ak::FormKind::record);
    assert(ak::to_list(ak::from_buffers(regular_record_buffers)) == ak::to_list(regular_records));

    const auto record_json = ak::to_json(record_buffers.form);
    assert(record_json == ak::to_json(record_buffers.form));
    const auto parsed_record_form = ak::form_from_json(record_json);
    assert(parsed_record_form == record_buffers.form);
    assert(ak::to_list(ak::from_buffers(parsed_record_form, record_buffers.length, record_buffers.buffers)) ==
           ak::to_list(records));

    const auto named_record_buffers = ak::to_buffers(ak::with_name(records, "Point\"\n"));
    assert(ak::form_from_json(ak::to_json(named_record_buffers.form)) == named_record_buffers.form);

    const auto string_buffers = ak::to_buffers(ak::from_iter({"one", "two"}));
    assert(ak::form_from_json(ak::to_json(string_buffers.form)) == string_buffers.form);

    const ak::BufferMap all_buffer_types{
        {"bool", std::vector<bool>{true, false}},
        {"bytes", std::vector<std::uint8_t>{1, 2, 3}},
        {"double", std::vector<double>{1.5, -2.0}},
        {"float", std::vector<float>{1.25F}},
        {"int", std::vector<std::int64_t>{-1, 2}},
        {"string", std::vector<std::string>{"one", std::string("two\0bytes", 9)}},
        {"uint", std::vector<std::uint64_t>{0, 9}},
    };
    const auto binary = ak::to_binary(all_buffer_types);
    assert(ak::buffers_from_binary(binary) == all_buffer_types);

    auto missing = nested_buffers;
    missing.buffers.erase("node1-data");
    assert_throws<std::invalid_argument>([&missing] {
        (void)ak::from_buffers(missing);
    });

    auto bad_length = nested_buffers;
    bad_length.length = 2;
    assert_throws<std::invalid_argument>([&bad_length] {
        (void)ak::from_buffers(bad_length);
    });
    assert_throws<std::invalid_argument>([] {
        (void)ak::form_from_json("{\"kind\":\"not-a-form\"}");
    });
    assert_throws<std::invalid_argument>([] {
        (void)ak::form_from_json("{\"kind\":\"numpy\"");
    });
    assert_throws<std::invalid_argument>([] {
        (void)ak::form_from_json("{}");
    });
    assert_throws<std::invalid_argument>([&record_json] {
        auto duplicate = record_json;
        duplicate.insert(1, "\"schema_version\":1,");
        (void)ak::form_from_json(duplicate);
    });
    assert_throws<std::invalid_argument>([&record_json] {
        auto incompatible = record_json;
        const auto version = incompatible.find("\"schema_version\":1");
        incompatible[version + std::string("\"schema_version\":").size()] = '2';
        (void)ak::form_from_json(incompatible);
    });
    assert_throws<std::invalid_argument>([&record_json] {
        auto unknown = record_json;
        const auto member = unknown.find("\"primitive\"");
        unknown.replace(member, std::string("\"primitive\"").size(), "\"unexpected\"");
        (void)ak::form_from_json(unknown);
    });
    assert_throws<std::invalid_argument>([binary] {
        auto truncated = binary;
        truncated.pop_back();
        (void)ak::buffers_from_binary(truncated);
    });
    assert_throws<std::invalid_argument>([] {
        auto invalid_tag = ak::to_binary({{"x", std::vector<std::uint8_t>{1}}});
        invalid_tag[25] = 255;
        (void)ak::buffers_from_binary(invalid_tag);
    });
    assert_throws<std::invalid_argument>([] {
        auto duplicate = ak::to_binary({{"aa", std::vector<std::uint8_t>{1}},
                                        {"bb", std::vector<std::uint8_t>{2}}});
        const std::vector<std::uint8_t> needle{'b', 'b'};
        const auto found = std::search(duplicate.begin(), duplicate.end(), needle.begin(), needle.end());
        assert(found != duplicate.end());
        *found = 'a';
        *(found + 1) = 'a';
        (void)ak::buffers_from_binary(duplicate);
    });
    assert_throws<std::invalid_argument>([binary] {
        auto trailing = binary;
        trailing.push_back(0);
        (void)ak::buffers_from_binary(trailing);
    });

    return 0;
}
