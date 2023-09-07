// This is a simple implementation of a jagged array in C++
// Author: Mohamed Elashri
// Date: 09/07/2023
#include <iostream>
#include <vector>
#include <stdexcept>
#include <algorithm>
#include <typeinfo>
#include <numeric>
#include <iterator>

/*
Avaiable Methods:
- get(int outerIndex, int innerIndex): Retrieve the element at specified indices.
- set(int outerIndex, int innerIndex, T value): Set the element at specified indices.
- append(const std::vector<T>& newVec): Append a new inner vector to the jagged array.
- flatten(): Flatten the jagged array into a single vector.
- printMatrixForm(): Print the jagged array in matrix form.
- reshape(size_t newSize, const T& padValue): Reshape each inner array to a specified size, padding with a default value.
- pad(const T& padValue): Pad each inner array to the length of the longest inner array.
- clip(size_t maxSize): Clip each inner array to a specified maximum size.
- concat(const JaggedArray& other, int axis): Concatenate another JaggedArray along a specified axis.
- len(): Get the length (number of rows).
- shape(): Get the shape.
- type(): Get the data type.
- maskBasedOnSum(T threshold): Boolean masking based on the sum of the sub-array.
- fancyIndexing(const std::vector<int>& indices): Fancy indexing.
- all(): Boolean masking based on all elements.
- any(): Boolean masking based on any elements.
- sum(): Get the sum of all elements.
- prod(): Get the product of all elements.
- max(): Get the maximum element.
- min(): Get the minimum element.
- var(): Get the variance.
- std(): Get the standard deviation.
- mean(): Get the mean.
- moment(int n): Get the nth moment.
- sort(): Sort each inner vector in ascending order.
- argsort(): Returns a new JaggedArray where each inner vector contains the indices that would sort it.
- mask(const std::vector<std::vector<bool>>& mask): Mask the jagged array based on a boolean mask.
- drop_none(): Drop all the null values.
- pad_none(size_t target_size): Pad each inner array to the length of the longest inner array.
- fill_none(const T& fill_value): Fill all the null values with a specified value.
*/

template <typename T>
class JaggedArray {
public:
    // Default constructor
    JaggedArray() : data(std::vector<std::vector<T>>()) {}

    // Constructor for initializing jagged array
    JaggedArray(const std::vector<std::vector<T>>& init_data) : data(init_data) {}

    // Method to append a new row
    void append(const std::vector<T>& newRow) {
        data.push_back(newRow);
    }

    // Method to get an element from a specific row and column
    T get(int row, int col) const {
        if (row < 0 || row >= data.size() || col < 0 || col >= data[row].size()) {
            throw std::out_of_range("Index out of range");
        }
        return data[row][col];
    }

    // Method to flatten the jagged array into a single vector
    std::vector<T> flatten() const {
        std::vector<T> flatArray;
        for (const auto& innerVec : data) {
            for (const auto& element : innerVec) {
                flatArray.push_back(element);
            }
        }
        return flatArray;
    }

    // Method to print the jagged array in matrix form
    void printMatrixForm() const {
        for (const auto& innerVec : data) {
            for (const auto& element : innerVec) {
                std::cout << element << " ";
            }
            std::cout << std::endl;
        }
    }

    // Method to reshape each inner array to a specified size, padding with a default value
    void reshape(size_t newSize, const T& padValue) {
        for (auto& innerVec : data) {
            if (innerVec.size() < newSize) {
                innerVec.resize(newSize, padValue);
            } else if (innerVec.size() > newSize) {
                innerVec.resize(newSize);
            }
        }
    }

    // Method to pad each inner array to the length of the longest inner array
    void pad(const T& padValue) {
        size_t maxLength = 0;
        for (const auto& innerVec : data) {
            maxLength = std::max(maxLength, innerVec.size());
        }
        reshape(maxLength, padValue);
    }

    // Method to clip each inner array to a specified maximum size
    void clip(size_t maxSize) {
        for (auto& innerVec : data) {
            if (innerVec.size() > maxSize) {
                innerVec.resize(maxSize);
            }
        }
    }

    // Method for concatenating another JaggedArray along a specified axis
    void concat(const JaggedArray& other, int axis) {
        if (axis == 0) {
            // Concatenate along the row axis
            data.insert(data.end(), other.data.begin(), other.data.end());
        } else if (axis == 1) {
            // Concatenate along the column axis
            if (data.size() != other.data.size()) {
                throw std::invalid_argument("Number of rows must be the same for axis=1 concatenation.");
            }
            for (size_t i = 0; i < data.size(); ++i) {
                data[i].insert(data[i].end(), other.data[i].begin(), other.data[i].end());
            }
        } else {
            throw std::invalid_argument("Invalid axis. Use 0 for rows and 1 for columns.");
        }
    }
    // Method to get the data type
    const char* type() const {
        const char* typeIdName = typeid(T).name();
        if (std::string(typeIdName) == "i") {
            return "int";
        } else if (std::string(typeIdName) == "d") {
            return "double";
        } // Add more cases here as needed
        else {
            return typeid(T).name();
        }
    }

    // Method to get the shape
    std::vector<size_t> shape() const {
        std::vector<size_t> shapeVec;
        for (const auto& innerVec : data) {
            shapeVec.push_back(innerVec.size());
        }
        return shapeVec;
    }

    // Method to get the length (number of rows)
    size_t len() const {
        return data.size();
    }

    // Method for Boolean masking based on the sum of the sub-array
    JaggedArray maskBasedOnSum(T threshold) {
        std::vector<std::vector<T>> new_data;
        for (const auto& innerVec : data) {
            if (std::accumulate(innerVec.begin(), innerVec.end(), 0) > threshold) {
                new_data.push_back(innerVec);
            }
        }
        return JaggedArray(new_data);
    }

    // Method for fancy indexing based on a vector of indices
    JaggedArray fancyIndexing(const std::vector<size_t>& indices) {
        std::vector<std::vector<T>> new_data;
        for (const auto& index : indices) {
            if (index < data.size()) {
                new_data.push_back(data[index]);
            } else {
                throw std::out_of_range("Index out of range.");
            }
        }
        return JaggedArray(new_data);
    }

////////////////////////     Reduction Methods         ////////////////////////
    std::vector<bool> all() {
        std::vector<bool> result;
        for (const auto& innerVec : data) {
            result.push_back(std::all_of(innerVec.begin(), innerVec.end(), [](T x) { return x; }));
        }
        return result;
    }

    std::vector<bool> any() {
        std::vector<bool> result;
        for (const auto& innerVec : data) {
            result.push_back(std::any_of(innerVec.begin(), innerVec.end(), [](T x) { return x; }));
        }
        return result;
    }

    std::vector<T> sum() {
        std::vector<T> result;
        for (const auto& innerVec : data) {
            result.push_back(std::accumulate(innerVec.begin(), innerVec.end(), static_cast<T>(0)));
        }
        return result;
    }

    std::vector<T> prod() {
        std::vector<T> result;
        for (const auto& innerVec : data) {
            T product = 1;
            for (const auto& elem : innerVec) {
                product *= elem;
            }
            result.push_back(product);
        }
        return result;
    }

    std::vector<T> max() {
        std::vector<T> result;
        for (const auto& innerVec : data) {
            if (!innerVec.empty()) {
                result.push_back(*std::max_element(innerVec.begin(), innerVec.end()));
            }
        }
        return result;
    }

    std::vector<T> min() {
        std::vector<T> result;
        for (const auto& innerVec : data) {
            if (!innerVec.empty()) {
                result.push_back(*std::min_element(innerVec.begin(), innerVec.end()));
            }
        }
        return result;
    }
/////////////////////////////////////////////////////////////////////////////////

////////////////////////     Statistical Methods         ////////////////////////
    std::vector<double> moment(int n) {
        std::vector<double> result;
        for (const auto& innerVec : data) {
            double sum = 0.0;
            for (const auto& elem : innerVec) {
                sum += std::pow(elem, n);
            }
            double moment_val = (innerVec.size() > 0) ? sum / innerVec.size() : 0.0;
            result.push_back(moment_val);
        }
        return result;
    }

    std::vector<double> mean() {
        std::vector<double> result;
        for (const auto& innerVec : data) {
            double mean_val = (innerVec.size() > 0) ? std::accumulate(innerVec.begin(), innerVec.end(), 0.0) / innerVec.size() : 0.0;
            result.push_back(mean_val);
        }
        return result;
    }

    std::vector<double> var() {
        std::vector<double> result;
        for (const auto& innerVec : data) {
            double mean_val = (innerVec.size() > 0) ? std::accumulate(innerVec.begin(), innerVec.end(), 0.0) / innerVec.size() : 0.0;
            double sum = 0.0;
            for (const auto& elem : innerVec) {
                sum += std::pow(elem - mean_val, 2);
            }
            double var_val = (innerVec.size() > 0) ? sum / innerVec.size() : 0.0;
            result.push_back(var_val);
        }
        return result;
    }

    std::vector<double> std() {
        std::vector<double> result;
        for (const auto& innerVec : data) {
            double mean_val = (innerVec.size() > 0) ? std::accumulate(innerVec.begin(), innerVec.end(), 0.0) / innerVec.size() : 0.0;
            double sum = 0.0;
            for (const auto& elem : innerVec) {
                sum += std::pow(elem - mean_val, 2);
            }
            double std_val = (innerVec.size() > 0) ? std::sqrt(sum / innerVec.size()) : 0.0;
            result.push_back(std_val);
        }
        return result;
    }
/////////////////////////////////////////////////////////////////////////////////

////////////////////////     Sorting Methods         ///////////////////////////

    // Method to Sort each inner vector in ascending order.
    void sort() {
        for (auto& innerVec : data) {
            std::sort(innerVec.begin(), innerVec.end());
        }
    }

    // Method that returns a new JaggedArray where each inner vector contains the indices that would sort it.
    JaggedArray<int> argsort() {
        JaggedArray<int> sortedIndices;
        for (const auto& innerVec : data) {
            std::vector<int> indices(innerVec.size());
            std::iota(indices.begin(), indices.end(), 0);
            std::sort(indices.begin(), indices.end(),
                      [&innerVec](int a, int b) { return innerVec[a] < innerVec[b]; });
            sortedIndices.append(indices);
        }
        return sortedIndices;
    }

/////////////////////////////////////////////////////////////////////////////////


//////////////////////////     I/O Methods         /////////////////////////////

    // Method to print the jagged array elements
void print() const {
    for (const auto& row : data) {
        for (const auto& element : row) {
            if (element) {
                std::cout << *element << ' ';
            } else {
                std::cout << "nullopt ";
            }
        }
        std::cout << '\n';
    }
}

////////////////////////////////////////////////////////////////////////////////////


////////////////////////     Missing values Methods         ////////////////////////

// Method to mask the jagged array based on a boolean mask
JaggedArray<std::optional<T>> mask(const std::vector<std::vector<bool>>& mask) {
    JaggedArray<std::optional<T>> result;
    for (size_t i = 0; i < data.size(); ++i) {
        std::vector<std::optional<T>> maskedRow;
        for (size_t j = 0; j < data[i].size(); ++j) {
            if (mask[i][j]) {
                maskedRow.push_back(data[i][j]);
            } else {
                maskedRow.push_back(std::nullopt);
            }
        }
        result.append(maskedRow);
    }
    return result;
}

// Method to drop all the null values
JaggedArray<T> drop_none() {
    JaggedArray<T> result;
    for (const auto& row : data) {
        std::vector<T> newRow;
        for (const auto& val : row) {
            if (val) {
                newRow.push_back(val.value());
            }
        }
        result.append(newRow);
    }
    return result;
}


// Method to pad each inner array to the length of the longest inner array

void pad_none(size_t target_size) {
    for (auto& row : data) {
        while (row.size() < target_size) {
            row.push_back(std::nullopt);
        }
    }
}

// Method to fill all the null values with a specified value

void fill_none(const T& fill_value) {
    for (auto& row : data) {
        for (auto& val : row) {
            if (!val.has_value()) {
                val = fill_value;
            }
        }
    }
}





////////////////////////////////////////////////////////////////////////////////////
private:
    std::vector<std::vector<T>> data;
};
