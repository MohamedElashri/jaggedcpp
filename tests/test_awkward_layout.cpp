#include <awkward/awkward.hpp>

#include <cassert>
#include <cstddef>
#include <memory>
#include <stdexcept>
#include <string>
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
    const ak::Array empty;
    assert(empty.length() == 0);
    assert(empty.nbytes() == 0);
    assert(empty.is_valid());
    assert(ak::to_list(empty) == list({}));
    assert(empty.typestr() == "0 * unknown");
    assert(empty.type().kind() == ak::TypeKind::array);
    assert(empty.type().typestr() == empty.typestr());
    assert(empty.ndim() == 1);

    const auto flat = ak::from_iter({1, 2, 3});
    assert(flat.length() == 3);
    assert(flat.is_valid());
    assert(flat.typestr() == "3 * int64");
    assert(ak::to_list(flat) == list({1, 2, 3}));
    assert(ak::type(flat).content().kind() == ak::TypeKind::numpy);
    assert(flat.ndim() == 1);
    assert(flat.scalar_at(1).value() == ak::Value(2));
    assert(flat.scalar_at(1).typestr() == "int64");
    assert(ak::type(flat.scalar_at(1)).kind() == ak::TypeKind::scalar);
    assert(flat.at(1).is<std::int64_t>());
    assert(flat.at(1).get<std::int64_t>() == 2);
    assert_throws<std::invalid_argument>([&flat] { (void)flat.at(1).get<double>(); });
    const auto flat_view = flat.view<int>();
    assert(flat_view.size() == 3);
    assert(flat_view.at(2) == 3);
    assert(std::vector<int>(flat_view.begin(), flat_view.end()) ==
           std::vector<int>({1, 2, 3}));
    assert_throws<std::invalid_argument>([&flat] { (void)flat.view<double>(); });

    const auto nested = ak::from_iter({{1, 2}, {}, {3, 4, 5}});
    assert(nested.length() == 3);
    assert(nested.typestr() == "3 * var * int64");
    assert(ak::to_list(nested) == list({list({1, 2}), list({}), list({3, 4, 5})}));
    assert(ak::num(nested) == std::vector<std::size_t>({2, 0, 3}));
    assert(nested.ndim() == 2);
    assert(nested.type().content().kind() == ak::TypeKind::list);

    const auto flattened = ak::flatten(nested);
    assert(flattened.typestr() == "5 * int64");
    assert(ak::to_list(flattened) == list({1, 2, 3, 4, 5}));
    assert(ak::to_list(ak::ravel(nested)) == ak::to_list(flattened));

    const auto unflattened = ak::unflatten(flattened, std::vector<std::size_t>{1, 3, 1});
    assert(ak::to_list(unflattened) == list({list({1}), list({2, 3, 4}), list({5})}));
    assert_throws<std::invalid_argument>([&flattened] {
        (void)ak::unflatten(flattened, std::vector<std::size_t>{2, 2});
    });

    const auto regular = ak::regular(std::vector<double>{1.0, 2.0, 3.0, 4.0}, 2);
    assert(regular.length() == 2);
    assert(regular.typestr() == "2 * 2 * float64");
    assert(ak::num(regular) == std::vector<std::size_t>({2, 2}));
    assert(ak::to_list(regular) == list({list({1.0, 2.0}), list({3.0, 4.0})}));
    assert(ak::to_list(ak::flatten(regular)) == list({1.0, 2.0, 3.0, 4.0}));

    const ak::Array zero_regular(std::make_shared<ak::RegularArray<int>>(
        std::vector<int>{}, 0, 3));
    assert(zero_regular.typestr() == "3 * 0 * int64");
    assert(ak::to_list(zero_regular) == list({list({}), list({}), list({})}));
    assert(ak::num(zero_regular) == std::vector<std::size_t>({0, 0, 0}));

    const auto strings = ak::from_iter({{"one", "two"}, {}, {"three"}});
    assert(strings.typestr() == "3 * var * string");
    assert(ak::to_list(strings) == list({list({"one", "two"}), list({}), list({"three"})}));

    const auto content = ak::from_iter({10, 20, 30, 40, 50});
    const ak::Array list_array(std::make_shared<ak::ListArray>(
        std::vector<std::size_t>{0, 3, 1}, std::vector<std::size_t>{2, 5, 1}, content.layout_ptr()));
    assert(list_array.typestr() == "3 * var * int64");
    assert(ak::to_list(list_array) == list({list({10, 20}), list({40, 50}), list({})}));
    assert(list_array.at(1, 0) == ak::Value(40));
    assert(ak::num(list_array) == std::vector<std::size_t>({2, 2, 0}));
    assert(ak::to_list(ak::flatten(list_array)) == list({10, 20, 40, 50}));
    assert(ak::to_list(ak::to_packed(list_array)) == ak::to_list(list_array));
    assert(ak::to_list(list_array.slice({ak::index::all(), ak::index::range(0, 1)})) ==
           list({list({10}), list({40}), list({})}));
    assert(ak::to_list(list_array.slice({ak::index::integers({0, 1}), ak::index::at(0)})) ==
           list({10, 40}));

    const ak::Array indexed(std::make_shared<ak::IndexedArray>(
        std::vector<std::ptrdiff_t>{2, 0, 2}, content.layout_ptr()));
    assert(indexed.typestr() == "3 * int64");
    assert(ak::to_list(indexed) == list({30, 10, 30}));
    assert(ak::to_list(ak::to_packed(indexed)) == list({30, 10, 30}));

    const ak::Array indexed_lists(std::make_shared<ak::IndexedArray>(
        std::vector<std::ptrdiff_t>{2, 0}, list_array.layout_ptr()));
    assert(ak::to_list(indexed_lists) == list({list({}), list({10, 20})}));
    assert(ak::num(indexed_lists) == std::vector<std::size_t>({0, 2}));

    const auto packed = ak::to_packed(nested);
    assert(packed.is_valid());
    assert(ak::to_list(packed) == ak::to_list(nested));

    const auto annotated = ak::with_named_axis(
        ak::with_attrs(nested, {{"source", "unit-test"}}), "rows", 0);
    assert(annotated.attrs().at("source") == "unit-test");
    assert(annotated.named_axes().at("rows") == 0);
    const auto annotated_slice = annotated.slice({ak::index::range(0, 2)});
    assert(annotated_slice.attrs() == annotated.attrs());
    assert(annotated_slice.named_axes() == annotated.named_axes());
    assert(ak::without_named_axis(annotated, "rows").named_axes().empty());
    const auto packed_annotated = ak::to_packed(annotated);
    assert(packed_annotated.attrs() == annotated.attrs());
    assert(packed_annotated.named_axes() == annotated.named_axes());
    const auto flattened_annotated = ak::flatten(ak::with_named_axis(annotated, "columns", 1));
    assert(flattened_annotated.named_axes() == ak::Array::NamedAxes({{"rows", 0}}));

    const auto concatenated_ints = ak::concatenate({ak::from_iter({1, 2}), ak::from_iter({3, 4})});
    assert(ak::to_list(concatenated_ints) == list({1, 2, 3, 4}));
    assert(concatenated_ints.layout().kind() == ak::LayoutKind::numpy);
    const auto native_concatenate = ak::concatenate({
        ak::from_iter<std::int64_t>({{1}, {2, 3}}),
        ak::from_iter<std::int64_t>({{4, 5}}),
    });
    assert(native_concatenate.layout().kind() == ak::LayoutKind::list_offset);
    assert(ak::to_buffers(native_concatenate).form.kind == ak::FormKind::list_offset);
    const auto identity_concatenate = ak::concatenate({annotated});
    assert(identity_concatenate.layout_ptr() == annotated.layout_ptr());
    assert(identity_concatenate.attrs() == annotated.attrs());
    assert(ak::to_list(ak::concatenate(
               {ak::from_iter({{1}, {2, 3}}), ak::from_iter({{4, 5}, {6}})}, 1)) ==
           list({list({1, 4, 5}), list({2, 3, 6})}));
    const auto heterogeneous = ak::concatenate({ak::from_iter({1, 2}), ak::from_iter({"three"})});
    assert(ak::to_list(heterogeneous) == list({1, 2, "three"}));
    assert(heterogeneous.layout().kind() == ak::LayoutKind::union_);

    assert_throws<std::invalid_argument>([] {
        ak::Array bad(std::make_shared<ak::ListOffsetArray<int>>(std::vector<int>{1}, std::vector<std::size_t>{0, 2}));
    });
    assert_throws<std::invalid_argument>([] {
        (void)ak::regular(std::vector<int>{1, 2, 3}, 2);
    });
    assert_throws<std::invalid_argument>([&flat] {
        (void)ak::num(flat);
    });
    assert_throws<std::invalid_argument>([&content] {
        (void)ak::Array(std::make_shared<ak::ListArray>(
            std::vector<std::size_t>{2}, std::vector<std::size_t>{1}, content.layout_ptr()));
    });
    assert_throws<std::invalid_argument>([&content] {
        (void)ak::Array(std::make_shared<ak::IndexedArray>(
            std::vector<std::ptrdiff_t>{-1}, content.layout_ptr()));
    });
    assert_throws<std::invalid_argument>([] {
        (void)ak::scalar(list({1, 2}));
    });
    assert_throws<std::invalid_argument>([] {
        (void)ak::concatenate({ak::from_iter({{1}, {2}}), ak::from_iter({{3}})}, 1);
    });
    assert_throws<std::invalid_argument>([&nested] {
        (void)ak::with_named_axis(nested, "bad", 2);
    });

    const auto empty_shifted = ak::add(empty, 1);
    assert(empty_shifted.layout_ptr() == empty.layout_ptr());

    const auto native_shifted = ak::add(ak::from_iter<std::int64_t>({{1, 2}, {}, {3}}), 10);
    assert(native_shifted.layout().kind() == ak::LayoutKind::list_offset);
    assert(ak::to_buffers(native_shifted).form.kind == ak::FormKind::list_offset);

    return 0;
}
