#include"../Jagged.h"

// Function to print a std::vector
template <typename T>
void printVector(const std::vector<T>& vec) {
    for (const auto& item : vec) {
        std::cout << item << " ";
    }
    std::cout << std::endl;
}

int main() {
    // Initialize a Jagged Array with integer type
    JaggedArray<int> jaggedArray({{1, 0, 0}, {0, 0, 0}, {2, 2, 2}, {3, 1, 1}, {4, 4}});

    // Print the initial jagged array
    std::cout << "Initial Jagged Array:" << std::endl;
    jaggedArray.printMatrixForm();
    
    // Use all() method and print result
    std::vector<bool> allResult = jaggedArray.all();
    std::cout << "\nResult of all(): ";
    printVector(allResult);

    // Use any() method and print result
    std::vector<bool> anyResult = jaggedArray.any();
    std::cout << "Result of any(): ";
    printVector(anyResult);

    // Use sum() method and print result
    std::vector<int> sumResult = jaggedArray.sum();
    std::cout << "Result of sum(): ";
    printVector(sumResult);

    // Use prod() method and print result
    std::vector<int> prodResult = jaggedArray.prod();
    std::cout << "Result of prod(): ";
    printVector(prodResult);

    // Use max() method and print result
    std::vector<int> maxResult = jaggedArray.max();
    std::cout << "Result of max(): ";
    printVector(maxResult);

    // Use min() method and print result
    std::vector<int> minResult = jaggedArray.min();
    std::cout << "Result of min(): ";
    printVector(minResult);

    return 0;
}