#include"../Jagged.h"



int main() {
    // Create a JaggedArray with some missing values
    std::vector<std::vector<std::optional<int>>> data = {{1, std::nullopt, 3}, {std::nullopt, 5, std::nullopt}, {7, std::nullopt, 9}};
    JaggedArray<std::optional<int>> jaggedArray(data);

    // Check for missing values
    auto noneArray = jaggedArray.is_none();

    // Print the result
    for (const auto& row : noneArray) {
        for (const auto& element : row) {
            std::cout << element << ' ';
        }
        std::cout << '\n';
    }

    return 0;
}
