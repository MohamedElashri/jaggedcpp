#include <jagged/jagged.hpp>

#include <array>
#include <cassert>
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

}  // namespace

int main() {
    const jagged::Array<int> empty;
    assert(empty.rows() == 0);
    assert(empty.values_size() == 0);
    assert(empty.empty());
    assert(empty.offsets().size() == 1);
    assert(empty.offsets()[0] == 0);

    const jagged::Array<int> flat({1, 2, 3}, {0, 2, 2, 3});
    assert(flat.rows() == 3);
    assert(flat.values_size() == 3);
    assert(!flat.empty());
    assert(flat.row_size(0) == 2);
    assert(flat.row_size(1) == 0);
    assert(flat.row_size(2) == 1);
    assert(flat.at(0, 1) == 2);
    assert(flat.at(2, 0) == 3);
    assert(flat.row(0)[0] == 1);
    assert(flat.row(0)[1] == 2);
    assert(flat.row(1).empty());
    assert(flat.flatten() == std::vector<int>({1, 2, 3}));
    assert(flat.to_rows() == std::vector<std::vector<int>>({{1, 2}, {}, {3}}));

    assert_throws<std::invalid_argument>([] { jagged::Array<int> bad({1}, {}); });
    assert_throws<std::invalid_argument>([] { jagged::Array<int> bad({1}, {1}); });
    assert_throws<std::invalid_argument>([] { jagged::Array<int> bad({1}, {0, 2}); });
    assert_throws<std::invalid_argument>([] { jagged::Array<int> bad({1, 2}, {0, 2, 1}); });
    assert_throws<std::out_of_range>([&flat] { (void)flat.row_size(3); });
    assert_throws<std::out_of_range>([&flat] { (void)flat.at(0, 2); });

    auto from_rows = jagged::Array<int>::from_rows({{4}, {}, {5, 6}});
    assert(from_rows.to_rows() == std::vector<std::vector<int>>({{4}, {}, {5, 6}}));

    const std::array<int, 3> appended_values{7, 8, 9};
    from_rows.reserve_values(16);
    from_rows.reserve_rows(8);
    from_rows.append_row(appended_values);
    from_rows.append_row({10});
    assert(from_rows.to_rows() == std::vector<std::vector<int>>({{4}, {}, {5, 6}, {7, 8, 9}, {10}}));

    from_rows.clear();
    assert(from_rows.rows() == 0);
    assert(from_rows.values_size() == 0);
    assert(from_rows.offsets().size() == 1);
    assert(from_rows.offsets()[0] == 0);

    return 0;
}
