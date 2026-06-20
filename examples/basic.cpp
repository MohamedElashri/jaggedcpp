#include <awkward/awkward.hpp>

#include <iostream>
#include <vector>

int main() {
    const auto array = ak::from_iter({{1, 2, 3}, {}, {4, 5}, {6, 7}});

    std::cout << "rows: " << array.length() << '\n';
    std::cout << "row sizes: ";
    for (const auto value : ak::num(array)) {
        std::cout << value << ' ';
    }
    std::cout << '\n';

    std::cout << "values: " << ak::to_list(ak::flatten(array)) << '\n';
    std::cout << "as list: " << ak::to_list(array) << '\n';

    const auto rebuilt = ak::unflatten(ak::flatten(array), std::vector<std::size_t>{3, 0, 2, 2});
    std::cout << "rebuilt: " << ak::to_list(rebuilt) << '\n';

    return 0;
}
