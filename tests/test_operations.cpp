#include <jagged/jagged.hpp>

#include <array>
#include <cassert>
#include <cmath>
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

void assert_near(double actual, double expected) {
    assert(std::fabs(actual - expected) < 1.0e-12);
}

}  // namespace

int main() {
    const auto array = jagged::Array<int>::from_rows({{3, 1}, {}, {2, 2, 2}, {5}});

    const auto concatenated = array.concat_rows(jagged::Array<int>::from_rows({{8}, {9, 10}}));
    assert(concatenated.to_rows() == std::vector<std::vector<int>>({{3, 1}, {}, {2, 2, 2}, {5}, {8}, {9, 10}}));

    const std::array<std::size_t, 3> indices{2, 0, 2};
    assert(array.take_rows(indices).to_rows() == std::vector<std::vector<int>>({{2, 2, 2}, {3, 1}, {2, 2, 2}}));
    assert_throws<std::out_of_range>([&array] {
        const std::array<std::size_t, 1> bad_index{4};
        (void)array.take_rows(bad_index);
    });

    const std::array<bool, 4> keep{true, false, true, false};
    assert(array.mask_rows(keep).to_rows() == std::vector<std::vector<int>>({{3, 1}, {2, 2, 2}}));
    assert(array.mask_rows(std::vector<bool>{false, true, false, true}).to_rows() == std::vector<std::vector<int>>({{}, {5}}));
    assert_throws<std::invalid_argument>([&array] {
        const std::array<bool, 1> bad_mask{true};
        (void)array.mask_rows(bad_mask);
    });

    assert(array.sort_rows().to_rows() == std::vector<std::vector<int>>({{1, 3}, {}, {2, 2, 2}, {5}}));

    assert(array.sum() == std::vector<int>({4, 0, 6, 5}));
    assert(array.product() == std::vector<int>({3, 1, 8, 5}));
    assert(array.min() == std::vector<std::optional<int>>({1, std::nullopt, 2, 5}));
    assert(array.max() == std::vector<std::optional<int>>({3, std::nullopt, 2, 5}));

    const auto means = array.mean();
    assert_near(*means[0], 2.0);
    assert(!means[1].has_value());
    assert_near(*means[2], 2.0);
    assert_near(*means[3], 5.0);

    const auto variances = array.variance();
    assert_near(*variances[0], 1.0);
    assert(!variances[1].has_value());
    assert_near(*variances[2], 0.0);
    assert_near(*variances[3], 0.0);

    const auto stddevs = array.stddev();
    assert_near(*stddevs[0], 1.0);
    assert(!stddevs[1].has_value());
    assert_near(*stddevs[2], 0.0);
    assert_near(*stddevs[3], 0.0);

    const auto doubles = jagged::Array<double>::from_rows({{0.5, 1.5}, {}});
    assert_near(doubles.sum()[0], 2.0);
    assert_near(*doubles.mean()[0], 1.0);
    assert(!doubles.mean()[1].has_value());

    return 0;
}
