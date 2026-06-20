#include <awkward/awkward.hpp>

#include <cassert>
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
    result.is_tuple = false;
    result.fields.reserve(values.size());
    result.values.reserve(values.size());
    for (auto& [name, value] : values) {
        result.fields.push_back(std::move(name));
        result.values.push_back(std::move(value));
    }
    return result;
}

ak::Value tuple(std::vector<ak::Value> values) {
    ak::Value::record_type result;
    result.is_tuple = true;
    result.fields.reserve(values.size());
    result.values = std::move(values);
    for (std::size_t i = 0; i < result.values.size(); ++i) {
        result.fields.push_back(std::to_string(i));
    }
    return result;
}

}  // namespace

int main() {
    const auto xs = ak::from_iter({1, 2, 3});
    const auto ys = ak::from_iter({{10, 11}, {}, {12}});
    const auto records = ak::zip({{"x", xs}, {"y", ys}}, {.depth_limit = 1});

    assert(records.length() == 3);
    assert(ak::fields(records) == std::vector<std::string>({"x", "y"}));
    assert(ak::to_list(records) ==
           list({
               record({{"x", 1}, {"y", list({10, 11})}}),
               record({{"x", 2}, {"y", list({})}}),
               record({{"x", 3}, {"y", list({12})}}),
           }));

    const auto first = records.record_at(0);
    assert(first.fields() == std::vector<std::string>({"x", "y"}));
    assert(first.field("x") == ak::Value(1));
    assert(ak::to_list(ak::field(records, "x")) == list({1, 2, 3}));
    assert(ak::to_list(records.slice({ak::index::field("y")})) == list({list({10, 11}), list({}), list({12})}));
    assert(ak::to_list(records.slice({ak::index::range(0, 2), ak::index::field("x")})) == list({1, 2}));

    const auto subset = ak::project_fields(records, {"y"});
    assert(ak::fields(records.slice({ak::index::fields({"y", "x"})})) ==
           std::vector<std::string>({"y", "x"}));
    assert(ak::fields(subset) == std::vector<std::string>({"y"}));
    assert(ak::to_list(subset) ==
           list({
               record({{"y", list({10, 11})}}),
               record({{"y", list({})}}),
               record({{"y", list({12})}}),
           }));

    const auto unzipped = ak::unzip(records);
    assert(unzipped.size() == 2);
    assert(ak::to_list(unzipped[0]) == list({1, 2, 3}));
    assert(ak::to_list(unzipped[1]) == list({list({10, 11}), list({}), list({12})}));

    const auto with_z = ak::with_field(records, ak::from_iter({"a", "b", "c"}), "z");
    assert(ak::fields(with_z) == std::vector<std::string>({"x", "y", "z"}));
    assert(ak::to_list(ak::field(with_z, "z")) == list({"a", "b", "c"}));
    const auto without_y = ak::without_field(with_z, "y");
    assert(ak::fields(without_y) == std::vector<std::string>({"x", "z"}));
    const auto empty_records = ak::without_field(ak::project_fields(records, {"x"}), "x");
    assert(empty_records.length() == records.length());
    assert(ak::fields(empty_records).empty());
    assert(ak::to_list(empty_records) == list({record({}), record({}), record({})}));

    const auto named = ak::with_name(records, "Point");
    assert(named.typestr() == "3 * Point{x: int64, y: var * int64}");

    const auto tuples = ak::zip(std::vector<ak::Array>{xs, ak::from_iter({4, 5, 6})}, {.depth_limit = 1});
    assert(tuples.is_tuple());
    assert(ak::fields(tuples) == std::vector<std::string>({"0", "1"}));
    assert(ak::to_list(tuples) == list({tuple({1, 4}), tuple({2, 5}), tuple({3, 6})}));
    assert(ak::to_list(ak::field(tuples, "1")) == list({4, 5, 6}));

    const auto nested_left = ak::from_iter({{1, 2}, {}, {3}});
    const auto nested_right = ak::from_iter({{10, 20}, {}, {30}});
    const auto nested_records = ak::zip({{"left", nested_left}, {"right", nested_right}});
    assert(ak::fields(nested_records) == std::vector<std::string>({"left", "right"}));
    assert(ak::to_list(nested_records) ==
           list({
               list({record({{"left", 1}, {"right", 10}}), record({{"left", 2}, {"right", 20}})}),
               list({}),
               list({record({{"left", 3}, {"right", 30}})}),
           }));
    assert(ak::to_list(ak::field(nested_records, "left")) == list({list({1, 2}), list({}), list({3})}));

    const auto nested_extra = ak::with_field(nested_records, ak::from_iter({{100, 200}, {}, {300}}), "extra");
    assert(ak::fields(nested_extra) == std::vector<std::string>({"left", "right", "extra"}));
    assert(ak::to_list(ak::field(nested_extra, "extra")) == list({list({100, 200}), list({}), list({300})}));
    assert(ak::fields(ak::without_field(nested_extra, "right")) == std::vector<std::string>({"left", "extra"}));

    assert_throws<std::invalid_argument>([&xs] {
        (void)ak::zip({{"x", xs}, {"bad", ak::from_iter({1, 2})}});
    });
    assert_throws<std::out_of_range>([&records] {
        (void)ak::field(records, "missing");
    });
    assert_throws<std::invalid_argument>([&tuples] {
        (void)ak::with_field(tuples, ak::from_iter({1, 2, 3}), "z");
    });

    return 0;
}
