#include <awkward/awkward.hpp>

#include <cassert>
#include <cstdint>
#include <memory>
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

ak::Value record(std::vector<std::pair<std::string, ak::Value>> values, bool is_tuple = false) {
    ak::Value::record_type result;
    result.is_tuple = is_tuple;
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
    ak::ArrayBuilder scalars;
    assert(scalars.length() == 0);
    assert(scalars.snapshot().to_list() == list({}));
    scalars.null();
    scalars.boolean(true);
    scalars.integer(42);
    scalars.real(2.5);
    scalars.string("hello");
    assert(scalars.length() == 5);
    assert(scalars.snapshot().to_list() == list({nullptr, true, 42.0, 2.5, "hello"}));

    ak::ArrayBuilder nested;
    nested.begin_list();
    nested.integer(1);
    nested.real(2.5);
    nested.null();
    nested.end_list();
    nested.begin_list();
    nested.string("three");
    nested.boolean(true);
    nested.begin_record();
    nested.field("x");
    nested.integer(4);
    nested.end_record();
    nested.end_list();
    const auto nested_snapshot = nested.snapshot();
    assert(nested_snapshot.to_list() ==
           list({list({1.0, 2.5, nullptr}), list({"three", true, record({{"x", 4}})})}));

    ak::ArrayBuilder structured;
    structured.begin_tuple(2);
    structured.index(1);
    structured.begin_list();
    structured.integer(1);
    structured.integer(2);
    structured.end_list();
    structured.index(0);
    structured.string("first");
    structured.end_tuple();

    structured.begin_record();
    structured.field("name");
    structured.string("point");
    structured.field("coordinates");
    structured.begin_list();
    structured.real(1.5);
    structured.real(2.5);
    structured.end_list();
    structured.field("valid");
    structured.boolean(true);
    structured.end_record();

    const auto structured_snapshot = structured.snapshot();
    assert(structured_snapshot.to_list() ==
           list({record({{"0", "first"}, {"1", list({1, 2})}}, true),
                 record({{"name", "point"}, {"coordinates", list({1.5, 2.5})}, {"valid", true}})}));

    const auto roundtrip = ak::from_buffers(ak::to_buffers(structured_snapshot));
    assert(roundtrip.to_list() == structured_snapshot.to_list());
    assert(roundtrip.layout().kind() == ak::LayoutKind::union_);

    ak::ArrayBuilder isolated;
    isolated.integer(1);
    const auto first_snapshot = isolated.snapshot();
    isolated.integer(2);
    assert(first_snapshot.to_list() == list({1}));
    assert(isolated.snapshot().to_list() == list({1, 2}));

    assert_throws<std::invalid_argument>([] {
        ak::ArrayBuilder builder;
        builder.end_list();
    });
    assert_throws<std::invalid_argument>([] {
        ak::ArrayBuilder builder;
        builder.begin_list();
        (void)builder.snapshot();
    });
    assert_throws<std::invalid_argument>([] {
        ak::ArrayBuilder builder;
        builder.begin_tuple(2);
        builder.index(0);
        builder.integer(1);
        builder.end_tuple();
    });
    assert_throws<std::out_of_range>([] {
        ak::ArrayBuilder builder;
        builder.begin_tuple(1);
        builder.index(1);
    });
    assert_throws<std::invalid_argument>([] {
        ak::ArrayBuilder builder;
        builder.begin_record();
        builder.integer(1);
    });
    assert_throws<std::invalid_argument>([] {
        ak::ArrayBuilder builder;
        builder.begin_record();
        builder.field("x");
        builder.integer(1);
        builder.field("x");
    });
    assert_throws<std::invalid_argument>([] {
        (void)ak::UnionArray(
            std::vector<std::uint8_t>{0}, std::vector<std::ptrdiff_t>{},
            {std::make_shared<ak::NumpyArray<std::int64_t>>(std::vector<std::int64_t>{1})});
    });
    assert_throws<std::invalid_argument>([] {
        (void)ak::UnionArray(
            std::vector<std::uint8_t>{1}, std::vector<std::ptrdiff_t>{0},
            {std::make_shared<ak::NumpyArray<std::int64_t>>(std::vector<std::int64_t>{1})});
    });
    assert_throws<std::invalid_argument>([] {
        (void)ak::UnionArray(
            std::vector<std::uint8_t>{0}, std::vector<std::ptrdiff_t>{1},
            {std::make_shared<ak::NumpyArray<std::int64_t>>(std::vector<std::int64_t>{1})});
    });

    return 0;
}
