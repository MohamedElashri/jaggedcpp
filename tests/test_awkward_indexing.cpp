#include <awkward/awkward.hpp>

#include <cassert>
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
    const ak::Array empty;
    assert(ak::to_list(empty.slice({ak::index::all()})) == list({}));

    const auto flat = ak::from_iter({30, 10, 20});
    assert(flat.at(0) == ak::Value(30));
    assert(flat.at(-1) == ak::Value(20));
    assert(ak::to_list(flat.slice({ak::index::range(0, 3, 2)})) == list({30, 20}));
    assert(ak::to_list(flat.slice({ak::index::range(std::nullopt, std::nullopt, -1)})) == list({20, 10, 30}));
    assert(ak::to_list(flat.slice({ak::index::integers({2, 0})})) == list({20, 30}));
    assert(ak::to_list(flat.slice({ak::index::booleans({false, true, true})})) == list({10, 20}));
    assert(ak::to_list(flat.slice({ak::index::array(ak::from_iter({true, false, true}))})) == list({30, 20}));
    assert(ak::to_list(flat.slice({ak::index::newaxis()})) == list({list({30, 10, 20})}));
    assert(ak::to_list(flat.slice({ak::index::all(), ak::index::newaxis()})) ==
           list({list({30}), list({10}), list({20})}));
    assert(ak::to_list(ak::local_index(flat)) == list({0, 1, 2}));
    assert_throws<std::out_of_range>([&flat] {
        (void)flat.at(3);
    });
    assert_throws<std::invalid_argument>([&flat] {
        (void)flat.slice({ak::index::booleans({true})});
    });

    const auto nested = ak::from_iter({{10, 11, 12}, {}, {13, 14}, {15}});
    assert(nested.at(0) == list({10, 11, 12}));
    assert(nested.at(-1, 0) == ak::Value(15));
    assert(ak::to_list(nested.slice({ak::index::range(0, 4, 2)})) == list({list({10, 11, 12}), list({13, 14})}));
    assert(ak::to_list(nested.slice({ak::index::integers({3, 0})})) == list({list({15}), list({10, 11, 12})}));
    assert(ak::to_list(nested.slice({ak::index::booleans({true, false, true, false})})) ==
           list({list({10, 11, 12}), list({13, 14})}));
    assert(ak::to_list(nested.slice({ak::index::all(), ak::index::range(0, 2)})) ==
           list({list({10, 11}), list({}), list({13, 14}), list({15})}));
    assert(ak::to_list(nested.slice({ak::index::at(0), ak::index::range(1, 3)})) == list({11, 12}));
    assert(ak::to_list(nested.slice({ak::index::integers({0, 2, 3}), ak::index::at(-1)})) == list({12, 14, 15}));
    assert(ak::to_list(nested.slice({ak::index::ellipsis(), ak::index::range(0, 1)})) ==
           list({list({10}), list({}), list({13}), list({15})}));
    assert(ak::to_list(nested.slice({ak::index::integers({0, 2, 3}), ak::index::integers({0, -1})})) ==
           list({list({10, 12}), list({13, 14}), list({15, 15})}));
    assert(ak::to_list(nested.slice({ak::index::at(0), ak::index::integers({-1, 0})})) == list({12, 10}));

    const auto ragged_mask = ak::from_iter({{true, false, true}, {}, {false, true}, {true}});
    assert(ak::to_list(nested.slice({ak::index::array(ragged_mask)})) ==
           list({list({10, 12}), list({}), list({14}), list({15})}));
    assert(ak::to_list(nested.slice({ak::index::all(), ak::index::array(ragged_mask)})) ==
           list({list({10, 12}), list({}), list({14}), list({15})}));
    assert(ak::to_list(ak::local_index(nested)) ==
           list({list({0, 1, 2}), list({}), list({0, 1}), list({0})}));

    assert_throws<std::out_of_range>([&nested] {
        (void)nested.at(1, 0);
    });
    assert_throws<std::invalid_argument>([&nested] {
        (void)nested.slice({ak::index::array(ak::from_iter({{true}, {false}}))});
    });
    assert_throws<std::invalid_argument>([&nested] {
        (void)nested.slice({ak::index::ellipsis(), ak::index::ellipsis()});
    });

    const auto regular = ak::regular(std::vector<int>{4, 1, 5, 2}, 2);
    assert(regular.at(1) == list({5, 2}));
    assert(ak::to_list(regular.slice({ak::index::all(), ak::index::range(0, 1)})) ==
           list({list({4}), list({5})}));
    assert(ak::to_list(ak::local_index(regular)) == list({list({0, 1}), list({0, 1})}));
    return 0;
}
