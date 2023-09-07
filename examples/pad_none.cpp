#include"../Jagged.h"

int main() {
    // Create a JaggedArray with some missing values
    std::vector<std::vector<std::optional<int>>> data = {{1, 2, 3}, {4, std::nullopt}, {7}};
    JaggedArray<std::optional<int>> jaggedArray(data);

    // Pad with missing values
    jaggedArray.pad_none(3);

    // Print the result
    jaggedArray.print();

    return 0;
}
