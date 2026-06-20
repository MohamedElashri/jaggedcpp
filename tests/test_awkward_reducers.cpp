#include <awkward/awkward.hpp>

#include <cassert>
#include <cmath>
#include <limits>
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

double as_double(const ak::Value& value) {
    if (const auto* real = std::get_if<double>(&value.storage())) {
        return *real;
    }
    return static_cast<double>(std::get<std::int64_t>(value.storage()));
}

void assert_near(const ak::Value& actual, double expected) {
    assert(std::fabs(as_double(actual) - expected) < 1.0e-12);
}

void assert_near_list(const ak::Value& actual, const std::vector<std::optional<double>>& expected) {
    const auto& values = actual.as_list();
    assert(values.size() == expected.size());
    for (std::size_t i = 0; i < expected.size(); ++i) {
        if (expected[i]) {
            assert_near(values[i], *expected[i]);
        } else {
            assert(values[i].is_none());
        }
    }
}

}  // namespace

int main() {
    const auto flat = ak::from_iter({1, 2, 3});
    assert(ak::count(flat) == 3);
    assert(ak::count_nonzero(flat) == 3);
    assert(ak::sum(flat) == 6);
    assert(ak::prod(flat) == 6);
    assert(ak::min(flat) == 1);
    assert(ak::max(flat) == 3);
    assert(ak::argmin(flat) == 0);
    assert(ak::argmax(flat) == 2);
    assert_near(ak::mean(flat), 2.0);

    const auto nested = ak::from_iter({{3, 1}, {}, {2, 2, 2}, {5}});
    assert(ak::sum(nested) == list({4, 0, 6, 5}));
    assert(ak::sum(nested, {.mask_identity = true}) == list({4, nullptr, 6, 5}));
    assert(ak::prod(nested) == list({3, 1, 8, 5}));
    assert(ak::any(ak::from_iter({{false, true}, {}, {false}})) == list({true, false, false}));
    assert(ak::all(ak::from_iter({{true, true}, {}, {false}})) == list({true, true, false}));
    assert(ak::min(nested) == list({1, nullptr, 2, 5}));
    assert(ak::max(nested) == list({3, nullptr, 2, 5}));
    assert(ak::argmin(nested) == list({1, nullptr, 0, 0}));
    assert(ak::argmax(nested) == list({0, nullptr, 0, 0}));
    assert(ak::sum(nested, {.keepdims = true}) == list({list({4}), list({0}), list({6}), list({5})}));
    assert(ak::sum(nested, {.axis = 0}) == list({10, 3, 2}));
    assert(ak::sum(nested, {.axis = std::nullopt}) == 15);

    assert_near_list(ak::mean(nested), {2.0, std::nullopt, 2.0, 5.0});
    assert_near_list(ak::var(nested), {1.0, std::nullopt, 0.0, 0.0});
    assert_near_list(ak::std(nested), {1.0, std::nullopt, 0.0, 0.0});
    assert_near_list(ak::moment(nested, 2), {1.0, std::nullopt, 0.0, 0.0});
    assert_near_list(ak::ptp(nested), {2.0, std::nullopt, 0.0, 0.0});

    const auto optioned = ak::from_iter<int>({{1, ak::none}, {}, {ak::none, 4}});
    assert(ak::count(optioned) == list({1, 0, 1}));
    assert(ak::sum(optioned) == list({1, 0, 4}));
    assert(ak::sum(optioned, {.mask_identity = true}) == list({1, nullptr, 4}));

    assert(ak::to_list(ak::sort(nested)) == list({list({1, 3}), list({}), list({2, 2, 2}), list({5})}));
    const auto axis_zero_input = ak::from_iter({{3, 1}, {2}, {4, 0}});
    assert(ak::to_list(ak::sort(axis_zero_input, {.axis = 0})) ==
           list({list({2, 0}), list({3}), list({4, 1})}));
    assert(ak::to_list(ak::argsort(axis_zero_input, {.axis = 0})) ==
           list({list({1, 2}), list({0}), list({2, 0})}));
    assert(ak::to_list(ak::argsort(nested)) == list({list({1, 0}), list({}), list({0, 1, 2}), list({0})}));
    assert(ak::to_list(ak::sort(flat, {.ascending = false})) == list({3, 2, 1}));
    assert(ak::to_list(ak::sort(optioned, {.ascending = false})) ==
           list({list({1, nullptr}), list({}), list({4, nullptr})}));

    const auto softmax = ak::to_list(ak::softmax(ak::from_iter({{1.0, 2.0}, {}, {3.0}})));
    const auto& softmax_rows = softmax.as_list();
    const auto& first_row = softmax_rows[0].as_list();
    assert_near(first_row[0], 1.0 / (1.0 + std::exp(1.0)));
    assert_near(first_row[1], std::exp(1.0) / (1.0 + std::exp(1.0)));
    assert(softmax_rows[1].as_list().empty());
    assert_near(softmax_rows[2].as_list()[0], 1.0);

    const auto floating = ak::from_iter({
        {1.0, std::numeric_limits<double>::quiet_NaN()},
        {std::numeric_limits<double>::quiet_NaN()},
        {2.0},
    });
    assert(ak::nansum(floating) == list({1.0, 0.0, 2.0}));
    assert_near_list(ak::nanmean(floating), {1.0, std::nullopt, 2.0});
    assert_near_list(ak::nanstd(floating), {0.0, std::nullopt, 0.0});

    ak::ArrayBuilder deep_builder;
    deep_builder.begin_list();
    deep_builder.begin_list();
    deep_builder.integer(3);
    deep_builder.integer(1);
    deep_builder.end_list();
    deep_builder.begin_list();
    deep_builder.integer(2);
    deep_builder.end_list();
    deep_builder.end_list();
    deep_builder.begin_list();
    deep_builder.begin_list();
    deep_builder.integer(4);
    deep_builder.integer(0);
    deep_builder.end_list();
    deep_builder.end_list();
    const auto deep = deep_builder.snapshot();
    assert(deep.ndim() == 3);
    assert(ak::to_list(deep.slice({ak::index::all(), ak::index::all(), ak::index::range(0, 1)})) ==
           list({list({list({3}), list({2})}), list({list({4})})}));
    assert(ak::to_list(deep.slice({ak::index::ellipsis(), ak::index::at(0)})) ==
           list({list({3, 2}), list({4})}));
    assert(ak::num(deep, 2) == list({list({2, 1}), list({2})}));
    assert(ak::to_list(ak::flatten(deep, 2)) ==
           list({list({3, 1, 2}), list({4, 0})}));
    assert(ak::to_list(ak::ravel(deep)) == list({3, 1, 2, 4, 0}));
    assert(ak::to_list(ak::local_index(deep)) ==
           list({list({list({0, 1}), list({0})}), list({list({0, 1})})}));
    assert(ak::to_list(ak::local_index(deep, 1)) == list({list({0, 1}), list({0})}));
    assert(ak::sum(deep, {.axis = 2}) == list({list({4, 2}), list({4})}));
    assert(ak::to_list(ak::sort(deep, {.axis = 2})) ==
           list({list({list({1, 3}), list({2})}), list({list({0, 4})})}));
    assert(ak::to_list(ak::argsort(deep, {.axis = 2})) ==
           list({list({list({1, 0}), list({0})}), list({list({1, 0})})}));
    const auto deep_softmax = ak::to_list(ak::softmax(deep, 2));
    assert_near(deep_softmax.as_list()[0].as_list()[0].as_list()[0], 1.0 / (1.0 + std::exp(-2.0)));
    assert_near(deep_softmax.as_list()[0].as_list()[0].as_list()[1], 1.0 / (1.0 + std::exp(2.0)));

    const auto scalar_result = ak::sum_result(flat);
    assert(std::holds_alternative<ak::Scalar>(scalar_result));
    assert(std::get<ak::Scalar>(scalar_result).value() == ak::Value(6));
    const auto annotated_nested = ak::with_named_axis(
        ak::with_attrs(nested, {{"source", "reducers"}}), "rows", 0);
    const auto array_result = ak::sum_result(annotated_nested);
    assert(std::holds_alternative<ak::Array>(array_result));
    assert(std::get<ak::Array>(array_result).attrs() == annotated_nested.attrs());
    assert(std::get<ak::Array>(array_result).named_axes() == annotated_nested.named_axes());

    assert_throws<std::invalid_argument>([&flat] {
        (void)ak::sum(flat, {.axis = 1});
    });
    assert_throws<std::invalid_argument>([] {
        (void)ak::sum(ak::from_iter({"a", "b"}));
    });

    return 0;
}
