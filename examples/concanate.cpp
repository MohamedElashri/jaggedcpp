#include"../Jagged.h"

int main() {
    JaggedArray<int> jaggedArray1({{1, 2}, {3, 4, 5}});
    JaggedArray<int> jaggedArray2({{6, 7}, {8}});

    std::cout << "JaggedArray1 before concatenation:" << std::endl;
    jaggedArray1.printMatrixForm();

    std::cout << "JaggedArray2:" << std::endl;
    jaggedArray2.printMatrixForm();

    // Concatenate along the row axis
    jaggedArray1.concat(jaggedArray2, 0);

    std::cout << "JaggedArray1 after concatenating along the row axis:" << std::endl;
    jaggedArray1.printMatrixForm();

    // Concatenate along the column axis
    // This should throw an error as the two jagged arrays do not have the same number of rows
    try {
        jaggedArray1.concat(jaggedArray2, 1);
    } catch (const std::invalid_argument& e) {
        std::cout << e.what() << std::endl;
    }

    return 0;
}
