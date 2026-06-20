#include <awkward/awkward.hpp>

#include <cassert>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
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
    const auto ragged = ak::from_iter({{1, 2}, {}, {3}});
    const auto broadcasted_scalar = ak::broadcast_arrays(ragged, 10);
    assert(ak::to_list(broadcasted_scalar.first) == list({list({1, 2}), list({}), list({3})}));
    assert(ak::to_list(broadcasted_scalar.second) == list({list({10, 10}), list({}), list({10})}));
    assert(ak::to_list(ak::add(ragged, 10)) == list({list({11, 12}), list({}), list({13})}));
    assert(ak::to_list(ragged + 10) == list({list({11, 12}), list({}), list({13})}));
    assert(ak::to_list(2 * ragged) == list({list({2, 4}), list({}), list({6})}));
    assert(ak::to_list(ragged > 1) == list({list({false, true}), list({}), list({true})}));
    assert(ak::to_list(ak::add(ragged, ak::from_iter({10, 20}))) ==
           list({list({11, 22}), list({}), list({13})}));

    const auto annotated_ragged = ak::with_named_axis(
        ak::with_attrs(ragged, {{"source", "left"}}), "rows", 0);
    const auto annotated_result = ak::add(annotated_ragged, 10);
    assert(annotated_result.attrs() == annotated_ragged.attrs());
    assert(annotated_result.named_axes() == annotated_ragged.named_axes());
    assert(annotated_result.layout().kind() == ak::LayoutKind::list_offset);
    const auto conflicting = ak::with_attrs(ragged, {{"source", "right"}});
    assert_throws<std::invalid_argument>([&annotated_ragged, &conflicting] {
        (void)ak::add(annotated_ragged, conflicting);
    });

    const auto per_row = ak::from_iter({10, 20, 30});
    assert(ak::to_list(ak::add(per_row, ragged)) == list({list({11, 12}), list({}), list({33})}));
    assert(ak::to_list(ak::multiply(ragged, ak::from_iter({{2, 3}, {}, {4}}))) ==
           list({list({2, 6}), list({}), list({12})}));
    assert(ak::to_list(ak::divide(ragged, 2)) == list({list({0.5, 1.0}), list({}), list({1.5})}));
    const auto large_integers = ak::from_iter<std::int64_t>({9007199254740993LL});
    assert(ak::to_list(ak::add(large_integers, 1)) == list({9007199254740994LL}));

    const auto optioned = ak::from_iter<int>({{1, ak::none}, {2}});
    assert(ak::to_list(ak::add(optioned, ak::from_iter({{10, 20}, {30}}))) ==
           list({list({11, nullptr}), list({32})}));
    assert(ak::to_list(ak::greater(optioned, 1)) == list({list({false, nullptr}), list({true})}));

    const auto cond = ak::from_iter({{true, false}, {}, {false}});
    assert(ak::to_list(ak::where(cond, ragged, 0)) == list({list({1, 0}), list({}), list({0})}));
    const auto mixed_where = ak::where(ak::from_iter({true, false}), ak::from_iter({1, 2}), "other");
    assert(ak::to_list(mixed_where) == list({1, "other"}));
    assert(mixed_where.layout().kind() == ak::LayoutKind::union_);
    assert(ak::to_list(ak::logical_not(cond)) == list({list({false, true}), list({}), list({true})}));
    assert(ak::to_list(ak::logical_or(cond, ak::from_iter({{false, false}, {}, {true}}))) ==
           list({list({true, false}), list({}), list({true})}));

    const auto close_left = ak::from_iter({1.0, 2.0, std::numeric_limits<double>::quiet_NaN()});
    const auto close_right = ak::from_iter({1.0 + 1.0e-6, 2.1, std::numeric_limits<double>::quiet_NaN()});
    assert(ak::to_list(ak::isclose(close_left, close_right, {.equal_nan = true})) == list({true, false, true}));
    assert(ak::almost_equal(close_left, ak::from_iter({1.0, 2.0, std::numeric_limits<double>::quiet_NaN()}), {.equal_nan = true}));
    assert(!ak::array_equal(close_left, close_right, true));
    assert(ak::array_equal(ak::from_iter({1, 2, 3}), ak::from_iter({1.0, 2.0, 3.0})));

    assert(ak::to_list(ak::zeros_like(optioned)) == list({list({0, nullptr}), list({0})}));
    assert(ak::to_list(ak::ones_like(ragged)) == list({list({1, 1}), list({}), list({1})}));
    assert(ak::to_list(ak::full_like(ragged, 7)) == list({list({7, 7}), list({}), list({7})}));

    const auto records = ak::zip({{"x", ak::from_iter({1, 2})}, {"y", ak::from_iter({10, 20})}}, {.depth_limit = 1});
    const auto reordered = ak::zip({{"y", ak::from_iter({100, 200})}, {"x", ak::from_iter({3, 4})}}, {.depth_limit = 1});
    const auto aligned = ak::broadcast_fields(records, reordered);
    assert(ak::fields(aligned.first) == std::vector<std::string>({"x", "y"}));
    assert(ak::fields(aligned.second) == std::vector<std::string>({"x", "y"}));
    assert(ak::to_list(aligned.second) ==
           list({record({{"x", 3}, {"y", 100}}), record({{"x", 4}, {"y", 200}})}));
    assert(ak::to_list(ak::add(records, reordered)) ==
           list({record({{"x", 4}, {"y", 110}}), record({{"x", 6}, {"y", 220}})}));

    assert_throws<std::invalid_argument>([&ragged] {
        (void)ak::add(ragged, ak::from_iter({{1}, {2}, {3}}));
    });
    assert_throws<std::invalid_argument>([] {
        (void)ak::broadcast_arrays(ak::from_iter({1, 2}), ak::from_iter({1, 2, 3}));
    });
    assert_throws<std::invalid_argument>([&records] {
        (void)ak::broadcast_fields(records, ak::zip({{"z", ak::from_iter({1, 2})}}, {.depth_limit = 1}));
    });

    return 0;
}
