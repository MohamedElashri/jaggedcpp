
#include"../Jagged.h"


int main() {
    JaggedArray<int> jaggedArray({{1, 2}, {3, 4, 5}, {6, 7, 8}});
    std::cout << "Jagged array in matrix form:" << std::endl;
    jaggedArray.printMatrixForm();

    // Boolean masking based on sum
    // This should return only the sub-array with a sum greater than 6
    JaggedArray<int> maskedArray = jaggedArray.maskBasedOnSum(6);
    std::cout << "Masked array:" << std::endl;
    maskedArray.printMatrixForm();  

    // Fancy indexing
    // This should return the sub-array with index 0 and 2
    JaggedArray<int> fancyIndexedArray = jaggedArray.fancyIndexing({0, 2});
    std::cout << "Fancy indexed array:" << std::endl;
    fancyIndexedArray.printMatrixForm();

    return 0;
}
