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
    JaggedArray<int> jaggedArray({{1, 2, 3}, {4, 5, 6, 7}, {8, 9}});

    // Print the initial jagged array
    std::cout << "Initial Jagged Array:" << std::endl;
    jaggedArray.printMatrixForm();

    // Use moment() method and print result
    std::vector<double> momentResult = jaggedArray.moment(3);
    std::cout << "\nResult of moment(3): ";
    printVector(momentResult);

    // Use mean() method and print result
    std::vector<double> meanResult = jaggedArray.mean();
    std::cout << "Result of mean(): ";
    printVector(meanResult);

    // Use var() method and print result
    std::vector<double> varResult = jaggedArray.var();
    std::cout << "Result of var(): ";
    printVector(varResult);

    // Use std() method and print result
    std::vector<double> stdResult = jaggedArray.std();
    std::cout << "Result of std(): ";
    printVector(stdResult);

    return 0;
}
