#pragma once

#include <algorithm>
#include <cmath>
#include <concepts>
#include <cstddef>
#include <functional>
#include <initializer_list>
#include <iterator>
#include <numeric>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace jagged {

namespace detail {

template <typename T>
struct is_optional : std::false_type {};

template <typename T>
struct is_optional<std::optional<T>> : std::true_type {};

template <typename T>
inline constexpr bool is_optional_v = is_optional<T>::value;

template <typename T>
struct optional_value {};

template <typename T>
struct optional_value<std::optional<T>> {
    using type = T;
};

template <typename T>
inline constexpr bool is_numeric_v = std::is_arithmetic_v<T> && !std::same_as<T, bool>;

inline void validate_offsets(std::size_t value_count, const std::vector<std::size_t>& offsets) {
    if (offsets.empty()) {
        throw std::invalid_argument("jagged::Array offsets must contain at least the initial zero offset");
    }
    if (offsets.front() != 0) {
        throw std::invalid_argument("jagged::Array offsets must start at zero");
    }
    if (offsets.back() != value_count) {
        throw std::invalid_argument("jagged::Array final offset must equal values size");
    }
    if (!std::is_sorted(offsets.begin(), offsets.end())) {
        throw std::invalid_argument("jagged::Array offsets must be monotonic");
    }
}

}  // namespace detail

template <typename T>
class Array {
public:
    using value_type = T;
    using size_type = std::size_t;

    Array() = default;

    Array(const Array&) = default;
    Array& operator=(const Array&) = default;

    Array(Array&& other) : values_(std::move(other.values_)), offsets_(std::move(other.offsets_)) {
        other.reset_to_empty();
    }

    Array& operator=(Array&& other) {
        if (this != &other) {
            values_ = std::move(other.values_);
            offsets_ = std::move(other.offsets_);
            other.reset_to_empty();
        }
        return *this;
    }

    Array(std::vector<T> values, std::vector<size_type> offsets)
        : values_(std::move(values)), offsets_(std::move(offsets)) {
        detail::validate_offsets(values_.size(), offsets_);
    }

    static Array<T> from_rows(const std::vector<std::vector<T>>& rows) {
        std::vector<T> values;
        std::vector<size_type> offsets;
        offsets.reserve(rows.size() + 1);
        offsets.push_back(0);

        size_type value_count = 0;
        for (const auto& row_values : rows) {
            values.insert(values.end(), row_values.begin(), row_values.end());
            value_count += row_values.size();
            offsets.push_back(value_count);
        }

        return Array<T>(std::move(values), std::move(offsets));
    }

    std::vector<std::vector<T>> to_rows() const {
        std::vector<std::vector<T>> result;
        result.reserve(rows());

        for (size_type row_index = 0; row_index < rows(); ++row_index) {
            const auto start = offsets_[row_index];
            const auto stop = offsets_[row_index + 1];
            result.emplace_back(values_.begin() + static_cast<std::ptrdiff_t>(start),
                                values_.begin() + static_cast<std::ptrdiff_t>(stop));
        }

        return result;
    }

    size_type rows() const noexcept {
        return offsets_.size() - 1;
    }

    size_type values_size() const noexcept {
        return values_.size();
    }

    bool empty() const noexcept {
        return rows() == 0;
    }

    size_type row_size(size_type row_index) const {
        check_row_index(row_index);
        return offsets_[row_index + 1] - offsets_[row_index];
    }

    std::span<const T> values() const noexcept requires (!std::same_as<T, bool>) {
        return std::span<const T>(values_.data(), values_.size());
    }

    std::span<const size_type> offsets() const noexcept {
        return std::span<const size_type>(offsets_.data(), offsets_.size());
    }

    std::span<const T> row(size_type row_index) const requires (!std::same_as<T, bool>) {
        check_row_index(row_index);
        const auto start = offsets_[row_index];
        const auto count = offsets_[row_index + 1] - start;
        if (count == 0) {
            return {};
        }
        return std::span<const T>(values_.data() + start, count);
    }

    decltype(auto) at(size_type row_index, size_type column_index) const {
        check_row_index(row_index);
        const auto start = offsets_[row_index];
        const auto stop = offsets_[row_index + 1];
        if (column_index >= stop - start) {
            throw std::out_of_range("jagged::Array column index out of range");
        }

        const auto value_index = start + column_index;
        if constexpr (std::same_as<T, bool>) {
            return static_cast<bool>(values_[value_index]);
        } else {
            return static_cast<const T&>(values_[value_index]);
        }
    }

    std::vector<T> flatten() const {
        return values_;
    }

    void append_row(std::span<const T> row_values) {
        values_.insert(values_.end(), row_values.begin(), row_values.end());
        offsets_.push_back(values_.size());
    }

    void append_row(std::initializer_list<T> row_values) {
        values_.insert(values_.end(), row_values.begin(), row_values.end());
        offsets_.push_back(values_.size());
    }

    void clear() {
        reset_to_empty();
    }

    void reserve_values(size_type value_capacity) {
        values_.reserve(value_capacity);
    }

    void reserve_rows(size_type row_capacity) {
        offsets_.reserve(row_capacity + 1);
    }

    Array<T> concat_rows(const Array<T>& other) const {
        std::vector<T> values = values_;
        values.insert(values.end(), other.values_.begin(), other.values_.end());

        std::vector<size_type> offsets = offsets_;
        offsets.reserve(offsets_.size() + other.rows());
        const auto base = values_.size();
        for (size_type i = 1; i < other.offsets_.size(); ++i) {
            offsets.push_back(base + other.offsets_[i]);
        }

        return Array<T>(std::move(values), std::move(offsets));
    }

    Array<T> take_rows(std::span<const size_type> indices) const {
        std::vector<T> values;
        std::vector<size_type> offsets;
        offsets.reserve(indices.size() + 1);
        offsets.push_back(0);

        for (const auto row_index : indices) {
            check_row_index(row_index);
            append_row_to(values, offsets, row_index);
        }

        return Array<T>(std::move(values), std::move(offsets));
    }

    Array<T> mask_rows(std::span<const bool> keep) const {
        if (keep.size() != rows()) {
            throw std::invalid_argument("jagged::Array row mask length must equal row count");
        }

        std::vector<T> values;
        std::vector<size_type> offsets;
        offsets.reserve(rows() + 1);
        offsets.push_back(0);

        for (size_type row_index = 0; row_index < rows(); ++row_index) {
            if (keep[row_index]) {
                append_row_to(values, offsets, row_index);
            }
        }

        return Array<T>(std::move(values), std::move(offsets));
    }

    Array<T> mask_rows(const std::vector<bool>& keep) const {
        if (keep.size() != rows()) {
            throw std::invalid_argument("jagged::Array row mask length must equal row count");
        }

        std::vector<T> values;
        std::vector<size_type> offsets;
        offsets.reserve(rows() + 1);
        offsets.push_back(0);

        for (size_type row_index = 0; row_index < rows(); ++row_index) {
            if (keep[row_index]) {
                append_row_to(values, offsets, row_index);
            }
        }

        return Array<T>(std::move(values), std::move(offsets));
    }

    Array<T> sort_rows() const requires std::totally_ordered<T> {
        std::vector<T> values;
        std::vector<size_type> offsets;
        values.reserve(values_.size());
        offsets.reserve(offsets_.size());
        offsets.push_back(0);

        for (size_type row_index = 0; row_index < rows(); ++row_index) {
            std::vector<T> sorted_row(values_.begin() + static_cast<std::ptrdiff_t>(offsets_[row_index]),
                                      values_.begin() + static_cast<std::ptrdiff_t>(offsets_[row_index + 1]));
            std::sort(sorted_row.begin(), sorted_row.end());
            values.insert(values.end(), sorted_row.begin(), sorted_row.end());
            offsets.push_back(values.size());
        }

        return Array<T>(std::move(values), std::move(offsets));
    }

    std::vector<T> sum() const requires detail::is_numeric_v<T> {
        std::vector<T> result;
        result.reserve(rows());

        for_each_row([&result](auto begin, auto end) {
            result.push_back(std::accumulate(begin, end, T{}));
        });

        return result;
    }

    std::vector<T> product() const requires detail::is_numeric_v<T> {
        std::vector<T> result;
        result.reserve(rows());

        for_each_row([&result](auto begin, auto end) {
            result.push_back(std::accumulate(begin, end, T{1}, std::multiplies<T>()));
        });

        return result;
    }

    std::vector<std::optional<T>> min() const requires std::totally_ordered<T> {
        std::vector<std::optional<T>> result;
        result.reserve(rows());

        for_each_row([&result](auto begin, auto end) {
            if (begin == end) {
                result.push_back(std::nullopt);
            } else {
                result.push_back(*std::min_element(begin, end));
            }
        });

        return result;
    }

    std::vector<std::optional<T>> max() const requires std::totally_ordered<T> {
        std::vector<std::optional<T>> result;
        result.reserve(rows());

        for_each_row([&result](auto begin, auto end) {
            if (begin == end) {
                result.push_back(std::nullopt);
            } else {
                result.push_back(*std::max_element(begin, end));
            }
        });

        return result;
    }

    std::vector<std::optional<double>> mean() const requires detail::is_numeric_v<T> {
        std::vector<std::optional<double>> result;
        result.reserve(rows());

        for_each_row([&result](auto begin, auto end) {
            const auto count = std::distance(begin, end);
            if (count == 0) {
                result.push_back(std::nullopt);
                return;
            }

            const auto total = std::accumulate(begin, end, 0.0);
            result.push_back(total / static_cast<double>(count));
        });

        return result;
    }

    std::vector<std::optional<double>> variance() const requires detail::is_numeric_v<T> {
        std::vector<std::optional<double>> result;
        result.reserve(rows());

        for_each_row([&result](auto begin, auto end) {
            const auto count = std::distance(begin, end);
            if (count == 0) {
                result.push_back(std::nullopt);
                return;
            }

            const auto total = std::accumulate(begin, end, 0.0);
            const auto row_mean = total / static_cast<double>(count);
            double squared_delta_sum = 0.0;
            for (auto it = begin; it != end; ++it) {
                const auto delta = static_cast<double>(*it) - row_mean;
                squared_delta_sum += delta * delta;
            }
            result.push_back(squared_delta_sum / static_cast<double>(count));
        });

        return result;
    }

    std::vector<std::optional<double>> stddev() const requires detail::is_numeric_v<T> {
        auto variances = variance();
        for (auto& value : variances) {
            if (value.has_value()) {
                value = std::sqrt(*value);
            }
        }
        return variances;
    }

    template <typename U = T>
    Array<bool> is_none() const requires detail::is_optional_v<U> {
        std::vector<std::vector<bool>> rows_result;
        rows_result.reserve(rows());

        for (size_type row_index = 0; row_index < rows(); ++row_index) {
            std::vector<bool> row_result;
            row_result.reserve(row_size(row_index));
            const auto start = offsets_[row_index];
            const auto stop = offsets_[row_index + 1];
            for (auto i = start; i < stop; ++i) {
                row_result.push_back(!values_[i].has_value());
            }
            rows_result.push_back(std::move(row_result));
        }

        return Array<bool>::from_rows(rows_result);
    }

    template <typename U = T>
    Array<typename detail::optional_value<U>::type> drop_none() const requires detail::is_optional_v<U> {
        using Inner = typename detail::optional_value<U>::type;
        std::vector<std::vector<Inner>> rows_result;
        rows_result.reserve(rows());

        for (size_type row_index = 0; row_index < rows(); ++row_index) {
            std::vector<Inner> row_result;
            const auto start = offsets_[row_index];
            const auto stop = offsets_[row_index + 1];
            for (auto i = start; i < stop; ++i) {
                if (values_[i].has_value()) {
                    row_result.push_back(*values_[i]);
                }
            }
            rows_result.push_back(std::move(row_result));
        }

        return Array<Inner>::from_rows(rows_result);
    }

    template <typename U = T>
    Array<typename detail::optional_value<U>::type> fill_none(
        const typename detail::optional_value<U>::type& fill_value) const requires detail::is_optional_v<U> {
        using Inner = typename detail::optional_value<U>::type;
        std::vector<Inner> values;
        values.reserve(values_.size());

        for (const auto& value : values_) {
            values.push_back(value.value_or(fill_value));
        }

        return Array<Inner>(std::move(values), offsets_);
    }

    template <typename U = T>
    Array<U> pad_none(size_type target_row_size) const requires detail::is_optional_v<U> {
        std::vector<U> values;
        std::vector<size_type> offsets;
        values.reserve(values_.size());
        offsets.reserve(rows() + 1);
        offsets.push_back(0);

        for (size_type row_index = 0; row_index < rows(); ++row_index) {
            const auto start = offsets_[row_index];
            const auto stop = offsets_[row_index + 1];
            values.insert(values.end(), values_.begin() + static_cast<std::ptrdiff_t>(start),
                          values_.begin() + static_cast<std::ptrdiff_t>(stop));
            const auto current_size = stop - start;
            if (current_size < target_row_size) {
                values.insert(values.end(), target_row_size - current_size, std::nullopt);
            }
            offsets.push_back(values.size());
        }

        return Array<U>(std::move(values), std::move(offsets));
    }

private:
    void check_row_index(size_type row_index) const {
        if (row_index >= rows()) {
            throw std::out_of_range("jagged::Array row index out of range");
        }
    }

    void append_row_to(std::vector<T>& values, std::vector<size_type>& offsets, size_type row_index) const {
        const auto start = offsets_[row_index];
        const auto stop = offsets_[row_index + 1];
        values.insert(values.end(), values_.begin() + static_cast<std::ptrdiff_t>(start),
                      values_.begin() + static_cast<std::ptrdiff_t>(stop));
        offsets.push_back(values.size());
    }

    template <typename Function>
    void for_each_row(Function function) const {
        for (size_type row_index = 0; row_index < rows(); ++row_index) {
            const auto start = offsets_[row_index];
            const auto stop = offsets_[row_index + 1];
            function(values_.begin() + static_cast<std::ptrdiff_t>(start),
                     values_.begin() + static_cast<std::ptrdiff_t>(stop));
        }
    }

    void reset_to_empty() {
        values_.clear();
        offsets_.clear();
        offsets_.push_back(0);
    }

    std::vector<T> values_;
    std::vector<size_type> offsets_{0};
};

}  // namespace jagged
