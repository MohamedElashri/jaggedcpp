#include"../Jagged.h"



/*
This is a demonstration of JaggedArray class implementation (Jagged.h)
The example does the following: 

`JaggedArray<int> jaggedArray({{1, 2}, {3, 4, 5}, {6}});:`
    Initializes a JaggedArray object named jaggedArray with integer data. 
    The jagged array is initialized with three "rows":
        - First row has two elements: 1, 2
        - Second row has three elements: 3, 4, 5
        - Third row has one element: 6

jaggedArray.append({7, 8, 9});: 
    Appends a new "row" to jaggedArray, with elements 7, 8, and 9.
-----------------------------------------------------------------------------------------------
```
try {
    int element = jaggedArray.get(1, 2);
    std::cout << "Element at (1,2): " << element << std::endl;
} catch (const std::out_of_range& e) {
    std::cout << e.what() << std::endl;
}
```

    This code segment is designed to access a specific element in jaggedArray: 
    It attempts to retrieve the element located at row 1 and column 2 (0-based indices) of jaggedArray, 
    which is the number 5. This value is then printed to the console.If the specified indices are out of range,
    an exception of type std::out_of_range will be thrown, and the program will output an error message.

-----------------------------------------------------------------------------------------------
```
std::vector<int> flatArray = jaggedArray.flatten();
for (const int& val : flatArray) {
    std::cout << val << " ";
}
std::cout << std::endl;
```
    This code segmant flattens jaggedArray into a single std::vector<int> and prints this flattened vector:
    The flattened array contains all elements from all rows of jaggedArray, stored in a single, one-dimensional vector.
    Then prints this flattened vector to the console.
-----------------------------------------------------------------------------------------------

```
std::cout << "Jagged array in matrix form:" << std::endl;
jaggedArray.printMatrixForm();
```
    This code segment prints the jagged array in matrix form.
----------------------------------------------------------------------------------------------    




*/



int main() {
    JaggedArray<int> jaggedArray({{1, 2}, {3, 4, 5}, {6}});
    jaggedArray.append({7, 8, 9});

    try {
        int element = jaggedArray.get(1, 2);
        std::cout << "Element at (1,2): " << element << std::endl;
    } catch (const std::out_of_range& e) {
        std::cout << e.what() << std::endl;
    }

    std::vector<int> flatArray = jaggedArray.flatten();
    for (const int& val : flatArray) {
        std::cout << val << " ";
    }
    std::cout <<  std::endl;

    std::cout << "Jagged array in matrix form:" << std::endl;
    jaggedArray.printMatrixForm();
    std::cout << "After reshaping to size 4 with padding value 0:" << std::endl;
    jaggedArray.reshape(4, 0);
    jaggedArray.printMatrixForm();

    std::cout << "After padding with value 0:" << std::endl;
    jaggedArray.pad(0);
    jaggedArray.printMatrixForm();

    std::cout << "After clipping to maximum size 2:" << std::endl;
    jaggedArray.clip(2);
    jaggedArray.printMatrixForm();
    // Getting the data type
    std::cout << "Data type: " << jaggedArray.type() << std::endl;

    // Getting the shape
    std::vector<size_t> shapeVec = jaggedArray.shape();
    std::cout << "Shape: [";
    for (size_t i = 0; i < shapeVec.size(); ++i) {
        std::cout << shapeVec[i];
        if (i < shapeVec.size() - 1) {
            std::cout << ", ";
        }
    }
    std::cout << "]" << std::endl;

    // Getting the length (number of rows)
    std::cout << "Length: " << jaggedArray.len() << std::endl;

    return 0;
}