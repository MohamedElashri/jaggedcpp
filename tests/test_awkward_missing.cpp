#include <awkward/awkward.hpp>

#include <cassert>
#include <cmath>
#include <cstddef>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
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

ak::Value list(std::vector<ak::Value> values) {
    return ak::Value(std::move(values));
}

}  // namespace

int main() {
    const auto flat = ak::from_iter<int>({1, ak::none, 3});
    assert(flat.length() == 3);
    assert(flat.typestr() == "3 * ?int64");
    assert(ak::to_list(flat) == list({1, nullptr, 3}));
    assert(flat.at(1).is_none());

    const std::vector<std::optional<int>> optional_values{1, std::nullopt, 5};
    assert(ak::to_list(ak::from_iter(optional_values)) == list({1, nullptr, 5}));

    const auto nested = ak::from_iter<int>({{1, ak::none}, {}, {ak::none, 4}});
    assert(nested.typestr() == "3 * var * ?int64");
    assert(ak::to_list(nested) == list({list({1, nullptr}), list({}), list({nullptr, 4})}));
    assert(nested.at(0, 1).is_none());

    assert(ak::to_list(ak::is_none(flat)) == list({false, true, false}));
    assert(ak::to_list(ak::is_none(nested, 1)) == list({list({false, true}), list({}), list({true, false})}));

    assert(ak::to_list(ak::drop_none(flat)) == list({1, 3}));
    assert(ak::to_list(ak::drop_none(nested)) == list({list({1}), list({}), list({4})}));
    assert(ak::to_list(ak::fill_none(nested, 9)) == list({list({1, 9}), list({}), list({9, 4})}));
    const auto mixed_fill = ak::fill_none(ak::from_iter<int>({1, ak::none, 3}), "missing");
    assert(ak::to_list(mixed_fill) == list({1, "missing", 3}));
    assert(mixed_fill.layout().kind() == ak::LayoutKind::union_);

    assert(ak::to_list(ak::pad_none(ak::from_iter({{1}, {2, 3}, {}}), 3)) ==
           list({list({1, nullptr, nullptr}), list({2, 3, nullptr}), list({nullptr, nullptr, nullptr})}));
    assert(ak::to_list(ak::pad_none(ak::from_iter({{1, 2, 3}, {4}}), 2, {.clip = true})) ==
           list({list({1, 2}), list({4, nullptr})}));

    const auto masked_rows = ak::mask(ak::from_iter({{1, 2}, {3}, {4, 5}}), ak::from_iter({true, false, true}));
    assert(ak::to_list(masked_rows) == list({list({1, 2}), nullptr, list({4, 5})}));
    assert(masked_rows.typestr() == "3 * ?var * int64");

    const auto masked_values =
        ak::mask(ak::from_iter({{1, 2}, {3}, {4, 5}}), ak::from_iter({{true, false}, {false}, {true, true}}));
    assert(ak::to_list(masked_values) == list({list({1, nullptr}), list({nullptr}), list({4, 5})}));

    assert(ak::to_list(ak::firsts(ak::from_iter({{10, 11}, {}, {12}}))) == list({10, nullptr, 12}));
    assert(ak::to_list(ak::singletons(flat)) == list({list({1}), list({}), list({3})}));

    ak::ArrayBuilder deep_builder;
    deep_builder.begin_list();
    deep_builder.begin_list();
    deep_builder.integer(1);
    deep_builder.null();
    deep_builder.end_list();
    deep_builder.end_list();
    deep_builder.begin_list();
    deep_builder.begin_list();
    deep_builder.null();
    deep_builder.integer(2);
    deep_builder.end_list();
    deep_builder.end_list();
    const auto deep_missing = deep_builder.snapshot();
    assert(ak::to_list(ak::is_none(deep_missing, -1)) ==
           list({list({list({false, true})}), list({list({true, false})})}));
    assert(ak::to_list(ak::drop_none(deep_missing, -1)) ==
           list({list({list({1})}), list({list({2})})}));
    assert(ak::to_list(ak::pad_none(deep_missing, 3, {.axis = -1})) ==
           list({list({list({1, nullptr, nullptr})}), list({list({nullptr, 2, nullptr})})}));
    assert(ak::to_list(ak::firsts(deep_missing, -1)) ==
           list({list({1}), list({nullptr})}));

    const auto floating = ak::from_iter(
        std::vector<double>{1.0, std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity()});
    assert(ak::to_list(ak::nan_to_none(floating)) == list({1.0, nullptr, std::numeric_limits<double>::infinity()}));
    assert(ak::to_list(ak::nan_to_num(floating, {.nan = -1.0, .posinf = 99.0})) == list({1.0, -1.0, 99.0}));

    const auto content = ak::from_iter({10, 20, 30});
    ak::Array indexed(std::make_shared<ak::IndexedOptionArray>(
        std::vector<std::ptrdiff_t>{0, -1, 2}, content.layout_ptr()));
    assert(ak::to_list(indexed) == list({10, nullptr, 30}));

    ak::Array byte_masked(std::make_shared<ak::ByteMaskedArray>(
        std::vector<std::uint8_t>{1, 0, 1}, content.layout_ptr(), true));
    assert(ak::to_list(byte_masked) == list({10, nullptr, 30}));

    ak::Array bit_masked(std::make_shared<ak::BitMaskedArray>(
        std::vector<std::uint8_t>{0b00000101}, content.layout_ptr(), 3, true, true));
    assert(ak::to_list(bit_masked) == list({10, nullptr, 30}));

    ak::Array unmasked(std::make_shared<ak::UnmaskedArray>(content.layout_ptr()));
    assert(unmasked.typestr() == "3 * ?int64");
    assert(ak::to_list(unmasked) == list({10, 20, 30}));

    assert_throws<std::invalid_argument>([&content] {
        ak::Array bad(std::make_shared<ak::BitMaskedArray>(
            std::vector<std::uint8_t>{}, content.layout_ptr(), 3, true, true));
    });
    assert_throws<std::invalid_argument>([] {
        (void)ak::mask(ak::from_iter({1, 2}), ak::from_iter({true}));
    });

    return 0;
}
