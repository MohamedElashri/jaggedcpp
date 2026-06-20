#include <awkward/awkward.hpp>

int main() {
    const auto array = ak::from_iter<int>({{1, ak::none}, {}, {3}});
    const auto filled = ak::fill_none(array, 0);

    if (filled.length() != 3 || filled.at(0, 1) != ak::Value(0)) {
        return 1;
    }
    return 0;
}
