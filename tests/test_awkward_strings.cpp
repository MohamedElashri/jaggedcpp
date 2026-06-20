#include <awkward/awkward.hpp>

#include <cassert>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

template <typename Exception, typename Function>
void expect_throws(Function&& function) {
    bool threw = false;
    try {
        function();
    } catch (const Exception&) {
        threw = true;
    }
    assert(threw);
}

ak::Value list(std::vector<ak::Value> values) {
    return ak::Value(std::move(values));
}

}  // namespace

int main() {
    const auto strings = ak::from_iter({"Alpha", "123", "", "two words"});
    assert(strings.typestr() == "4 * string");
    assert(strings.at(-1) == ak::Value("two words"));
    assert(ak::to_list(strings) == list({"Alpha", "123", "", "two words"}));

    const auto packed = ak::to_buffers(strings);
    assert(packed.form.kind == ak::FormKind::list_offset);
    assert(packed.form.parameters.at("__array__") == "string");
    assert(packed.form.contents.size() == 1);
    assert(packed.form.contents[0].primitive == "uint8");
    assert(packed.form.contents[0].parameters.at("__array__") == "char");
    assert(std::holds_alternative<std::vector<std::uint8_t>>(
        packed.buffers.at(packed.form.contents[0].key + "-data")));
    assert(ak::array_equal(strings, ak::from_buffers(packed)));

    const auto regular_strings = ak::regular(std::vector<std::string>{"a", "bb", "c", "dd"}, 2);
    const auto regular_buffers = ak::to_buffers(regular_strings);
    assert(regular_buffers.form.kind == ak::FormKind::regular);
    assert(regular_buffers.form.contents[0].parameters.at("__array__") == "string");
    assert(ak::array_equal(regular_strings, ak::from_buffers(regular_buffers)));

    const auto selected = strings.slice({ak::index::integers({3, 0})});
    assert(ak::to_list(selected) == list({"two words", "Alpha"}));

    const auto optional = ak::from_iter(std::vector<std::optional<std::string>>{
        std::string("abc"), std::nullopt, std::string("42"), std::string(" ")});
    assert(optional.typestr() == "4 * ?string");
    assert(ak::to_list(ak::str::is_alpha(optional)) == list({true, nullptr, false, false}));
    assert(ak::to_list(ak::str::is_digit(optional)) == list({false, nullptr, true, false}));
    assert(ak::to_list(ak::str::is_numeric(optional)) == list({false, nullptr, true, false}));
    assert(ak::to_list(ak::str::is_space(optional)) == list({false, nullptr, false, true}));

    assert(ak::to_list(ak::str::is_alnum(strings)) == list({true, true, false, false}));
    assert(ak::to_list(ak::str::is_ascii(strings)) == list({true, true, true, true}));
    assert(ak::to_list(ak::str::is_decimal(strings)) == list({false, true, false, false}));
    assert(ak::to_list(ak::str::is_printable(ak::from_iter({"", "ok", "bad\n"}))) ==
           list({true, true, false}));
    assert(ak::to_list(ak::str::is_lower(ak::from_iter({"abc", "a1", "ABC", "123"}))) ==
           list({true, true, false, false}));
    assert(ak::to_list(ak::str::is_upper(ak::from_iter({"ABC", "A1", "abc", "123"}))) ==
           list({true, true, false, false}));
    assert(ak::to_list(ak::str::is_title(ak::from_iter({"Hello World", "Hello world", "123"}))) ==
           list({true, false, false}));

    const auto nested = ak::from_iter({{"oNe", "TWO words"}, {}, {"three"}});
    assert(nested.typestr() == "3 * var * string");
    assert(ak::to_list(ak::str::lower(nested)) ==
           list({list({"one", "two words"}), list({}), list({"three"})}));
    assert(ak::to_list(ak::str::upper(nested)) ==
           list({list({"ONE", "TWO WORDS"}), list({}), list({"THREE"})}));
    assert(ak::to_list(ak::str::capitalize(nested)) ==
           list({list({"One", "Two words"}), list({}), list({"Three"})}));
    assert(ak::to_list(ak::str::title(nested)) ==
           list({list({"One", "Two Words"}), list({}), list({"Three"})}));
    const auto nested_optional = ak::from_iter<std::string>(
        {{ak::Option<std::string>("One"), ak::none}, {}, {ak::Option<std::string>("three")}});
    assert(ak::to_list(ak::str::upper(nested_optional)) ==
           list({list({"ONE", nullptr}), list({}), list({"THREE"})}));
    assert(ak::to_list(ak::str::reverse(ak::from_iter({"abc", ""}))) == list({"cba", ""}));

    assert(ak::to_list(ak::str::slice(ak::from_iter({"abcdef", "xy"}), 1, -1, 2)) == list({"bd", ""}));
    assert(ak::to_list(ak::str::slice(ak::from_iter({"abc", "xy"}), std::nullopt, std::nullopt, -1)) ==
           list({"cba", "yx"}));

    const auto split = ak::str::split_pattern(
        ak::from_iter(std::vector<std::optional<std::string>>{"a,b,c", std::nullopt, "", "x,,y"}), ",");
    assert(ak::to_list(split) ==
           list({list({"a", "b", "c"}), nullptr, list({""}), list({"x", "", "y"})}));
    assert(ak::to_list(ak::str::split_pattern(ak::from_iter({"a,b,c"}), ",", {.max_splits = 1})) ==
           list({list({"a", "b,c"})}));
    assert(ak::to_list(ak::str::join(split, "|")) == list({"a|b|c", nullptr, "", "x||y"}));

    assert(ak::to_list(ak::str::contains(optional, "b")) == list({true, nullptr, false, false}));
    assert(ak::to_list(ak::str::starts_with(strings, "Al")) == list({true, false, false, false}));
    assert(ak::to_list(ak::str::ends_with(strings, "words")) == list({false, false, false, true}));

    const auto utf8 = ak::from_iter({"caf\xC3\xA9"});
    assert(ak::to_list(ak::str::upper(utf8)) == list({"CAF\xC3\xA9"}));
    assert(ak::to_list(ak::str::is_ascii(utf8)) == list({false}));
    assert(ak::to_list(ak::str::is_alpha(utf8)) == list({false}));

    expect_throws<std::invalid_argument>([] { (void)ak::str::upper(ak::from_iter({1, 2})); });
    expect_throws<std::invalid_argument>([&] { (void)ak::str::slice(strings, std::nullopt, std::nullopt, 0); });
    expect_throws<std::invalid_argument>([&] { (void)ak::str::split_pattern(strings, ""); });
    expect_throws<std::invalid_argument>([] {
        (void)ak::StringArray(std::vector<std::uint8_t>{'a'}, std::vector<std::size_t>{0, 2});
    });
}
