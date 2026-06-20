#include <awkward/awkward.hpp>

#include <iostream>

int main() {
    const auto array = ak::from_iter<int>({
        {1, ak::none, 3},
        {},
        {ak::none, 5},
    });

    std::cout << "with missing values: " << ak::to_list(array) << '\n';
    std::cout << "filled: " << ak::to_list(ak::fill_none(array, 0)) << '\n';
    std::cout << "missing mask: " << ak::to_list(ak::is_none(array, 1)) << '\n';

    return 0;
}
