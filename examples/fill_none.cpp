#include"../Jagged.h"

int main() {
    // Create a JaggedArray with some missing values
    std::vector<std::vector<std::optional<int>>> data = {{1, std::nullopt, 3}, {std::nullopt, 5, std::nullopt}, {7, std::nullopt, 9}};
    JaggedArray<std::optional<int>> jaggedArray(data);

    // Fill missing values
    jaggedArray.fill_none(0);

    // Print the result
    jaggedArray.print();

    return 0;
}
