#include <awkward/awkward.hpp>

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <random>
#include <stdexcept>
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

}  // namespace

int main() {
    std::mt19937_64 generator(0x6a61676765646370ULL);
    std::uniform_int_distribution<int> row_count_distribution(0, 8);
    std::uniform_int_distribution<int> row_size_distribution(0, 7);
    std::uniform_int_distribution<int> value_distribution(-100, 100);

    for (std::size_t trial = 0; trial < 128; ++trial) {
        std::vector<std::vector<int>> rows;
        rows.resize(static_cast<std::size_t>(row_count_distribution(generator)));
        for (auto& row : rows) {
            row.resize(static_cast<std::size_t>(row_size_distribution(generator)));
            for (auto& value : row) value = value_distribution(generator);
        }

        const auto array = ak::from_iter(rows);
        assert(array.is_valid());
        assert(array.length() == rows.size());
        assert(ak::num(array).size() == rows.size());
        for (std::size_t row = 0; row < rows.size(); ++row) {
            assert(ak::num(array)[row] == rows[row].size());
        }

        const auto packed = ak::to_packed(array);
        assert(packed.is_valid());
        assert(ak::to_list(packed) == ak::to_list(array));

        const auto buffers = ak::to_buffers(array);
        assert(ak::to_list(ak::from_buffers(buffers)) == ak::to_list(array));
        assert(ak::form_from_json(ak::to_json(buffers.form)) == buffers.form);
        if (const auto found = buffers.buffers.find("node0-offsets"); found != buffers.buffers.end()) {
            const auto& offsets = std::get<std::vector<std::int64_t>>(found->second);
            assert(!offsets.empty());
            assert(offsets.front() == 0);
            assert(std::is_sorted(offsets.begin(), offsets.end()));
            assert(offsets.back() == static_cast<std::int64_t>(ak::flatten(array).length()));
        }

        const auto reversed = array.slice({ak::index::range(std::nullopt, std::nullopt, -1)});
        const auto restored = reversed.slice({ak::index::range(std::nullopt, std::nullopt, -1)});
        assert(ak::to_list(restored) == ak::to_list(array));

        std::vector<std::ptrdiff_t> even_rows;
        std::vector<std::vector<int>> expected_rows;
        for (std::size_t row = 0; row < rows.size(); row += 2) {
            even_rows.push_back(static_cast<std::ptrdiff_t>(row));
            expected_rows.push_back(rows[row]);
        }
        assert(ak::to_list(array.slice({ak::index::integers(even_rows)})) ==
               ak::to_list(ak::from_iter(expected_rows)));

        const auto midpoint = rows.size() / 2;
        std::vector<std::vector<int>> first(rows.begin(), rows.begin() + static_cast<std::ptrdiff_t>(midpoint));
        std::vector<std::vector<int>> second(rows.begin() + static_cast<std::ptrdiff_t>(midpoint), rows.end());
        assert(ak::to_list(ak::concatenate({ak::from_iter(first), ak::from_iter(second)})) == ak::to_list(array));
    }

    for (std::size_t content_length = 0; content_length < 8; ++content_length) {
        assert_throws<std::invalid_argument>([content_length] {
            (void)ak::Array(std::make_shared<ak::ListOffsetArray<int>>(
                std::vector<int>(content_length), std::vector<std::size_t>{0, content_length + 1}));
        });
        assert_throws<std::invalid_argument>([content_length] {
            (void)ak::Array(std::make_shared<ak::IndexedArray>(
                std::vector<std::ptrdiff_t>{static_cast<std::ptrdiff_t>(content_length)},
                ak::from_iter(std::vector<int>(content_length)).layout_ptr()));
        });
    }

    const auto option_content = ak::from_iter({10, 20, 30, 40});
    const std::vector<ak::Array> option_layouts{
        ak::Array(std::make_shared<ak::IndexedOptionArray>(
            std::vector<std::ptrdiff_t>{0, -1, 2, 3}, option_content.layout_ptr())),
        ak::Array(std::make_shared<ak::ByteMaskedArray>(
            std::vector<std::uint8_t>{1, 0, 1, 1}, option_content.layout_ptr(), true)),
        ak::Array(std::make_shared<ak::BitMaskedArray>(
            std::vector<std::uint8_t>{0b00001101}, option_content.layout_ptr(), 4, true, true)),
        ak::Array(std::make_shared<ak::UnmaskedArray>(option_content.layout_ptr())),
    };
    for (const auto& array : option_layouts) {
        const auto expected = array.to_list().as_list();
        auto reversed_expected = expected;
        std::reverse(reversed_expected.begin(), reversed_expected.end());
        assert(ak::to_list(array.slice({ak::index::range(std::nullopt, std::nullopt, -1)})) ==
               ak::Value(std::move(reversed_expected)));
        assert(ak::to_list(array.slice({ak::index::integers({3, 0})})) ==
               ak::Value::list_type({expected[3], expected[0]}));
        assert(ak::to_list(array.slice({ak::index::booleans({true, false, true, false})})) ==
               ak::Value::list_type({expected[0], expected[2]}));
    }

    return 0;
}
