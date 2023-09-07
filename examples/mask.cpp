#include"../Jagged.h"


int main() {
    // Create a JaggedArray
    std::vector<std::vector<int>> data = {{1, 2, 3}, {4, 5, 6}, {7, 8, 9}};
    JaggedArray<int> jaggedArray(data);

    // Create a mask
    std::vector<std::vector<bool>> mask = {{true, false, true}, {false, true, false}, {true, false, true}};

    // Apply the mask to the JaggedArray
    auto maskedArray = jaggedArray.mask(mask);

    // Print the result
    maskedArray.print();

    return 0;
}
