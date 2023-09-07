#include"../Jagged.h"

int main() {
    // Initialize a JaggedArray with integer type
    JaggedArray<int> jaggedArray({{3, 1, 2}, {5, 4}, {9, 8, 7, 6}});

    // Print the original JaggedArray
    std::cout << "Original JaggedArray: \n";
    // (Assume a method print() exists in your JaggedArray class to print the jagged array)
    jaggedArray.print();

    // Sort each sub-array in ascending order
    jaggedArray.sort();

    // Print the sorted JaggedArray
    std::cout << "Sorted JaggedArray: \n";
    jaggedArray.print();

    // Get the sorted indices using argsort()
    JaggedArray<int> sortedIndices = jaggedArray.argsort();

    // Print the sorted indices
    std::cout << "Sorted Indices: \n";
    sortedIndices.print();

    return 0;
}
