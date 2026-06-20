# Migrating from `jagged::Array<T>`

The primary API is now `ak::Array` plus free functions in the `ak` namespace.
The legacy `jagged::Array<T>` container remains available, but new features are
implemented on the compatibility API.

## Build and include changes

The project and installed package remain named `jaggedcpp`; no
`find_package(...)` rename is required. Link the preferred target and include
the compatibility umbrella header:

```cmake
find_package(jaggedcpp CONFIG REQUIRED)
target_link_libraries(my_target PRIVATE awkward::awkward)
```

```cpp
#include <awkward/awkward.hpp>
```

`jagged::jagged` is retained as a backward-compatible target and exposes the
same headers.

## Construction and access

```cpp
// Before
#include <jagged/jagged.hpp>

auto array = jagged::Array<int>::from_rows({{1, 2}, {}, {3}});
auto value = array.at(0, 1);

// After
#include <awkward/awkward.hpp>

auto array = ak::from_iter({{1, 2}, {}, {3}});
auto value = array.at(0, 1); // ak::Value
```

`ak::Array` is runtime-typed. Scalar access returns `ak::Value`, while
`ak::to_list(array)` returns a nested `ak::Value` tree suitable for comparison,
display, and shape-independent processing. Use `Value::is<T>()` and `get<T>()`
for exact checked variant access. Flat primitive arrays also expose a read-only
`array.view<T>()`; `T` must match the physical layout element type exactly.

| Legacy API | Compatibility API |
| --- | --- |
| `jagged::Array<T>::from_rows(rows)` | `ak::from_iter(rows)` |
| `array.rows()` | `array.length()` |
| `array.row_size(i)` | `ak::num(array).at(i)` |
| `array.at(i, j)` | `array.at(i, j)` returning `ak::Value` |
| `array.to_rows()` | `ak::to_list(array)` returning `ak::Value` |
| `array.flatten()` | `ak::flatten(array)` |
| `array.concat_rows(other)` | `ak::concatenate({array, other})` |
| `array.take_rows(indices)` | `array.slice({ak::index::integers(signed_indices)})` |
| `array.mask_rows(mask)` | `array.slice({ak::index::booleans(mask)})` |
| `array.sort_rows()` | `ak::sort(array)` |
| `array.sum()` | `ak::sum(array)` |
| `array.product()` | `ak::prod(array)` |
| `array.min()` / `array.max()` | `ak::min(array)` / `ak::max(array)` |
| `array.mean()` / `variance()` / `stddev()` | `ak::mean(array)` / `ak::var(array)` / `ak::std(array)` |

Reducers return `ak::Value` and accept option structs such as
`ak::ReducerOptions` and `ak::StatisticOptions`. Their default axis and
empty-list behavior follow the implemented Awkward-style semantics, so code
that depends on the legacy reducers' concrete `std::vector<T>` result types
must be adapted explicitly.

For code that needs the result category encoded in the type, use
`ak::sum_result` or `ak::mean_result`. They return
`ak::ReducerResult`, an explicit `std::variant<ak::Scalar, ak::Array>`.

`ak::index::integers` accepts `std::vector<std::ptrdiff_t>`, including negative
Awkward-style indexes. Convert legacy unsigned row-index vectors before using
that slice form.

## Missing values

Use `ak::none` when initializer-list type inference needs an explicit missing
sentinel. Existing `std::optional<T>` vectors are also accepted.

```cpp
auto array = ak::from_iter<int>({{1, ak::none}, {}, {ak::none, 4}});
auto mask = ak::is_none(array, 1);
auto dense = ak::fill_none(array, 0);
auto dropped = ak::drop_none(array);
auto padded = ak::pad_none(array, 3);
```

## Mutation

`ak::Array` shares an immutable layout. There are no direct equivalents for
`append_row`, `clear`, or the `reserve_*` methods. Use `ak::ArrayBuilder` for
incremental mixed or nested construction, then call `snapshot()` to obtain an
immutable array.

Use `ak::concatenate` for immutable row or nested-axis concatenation.

Mutable legacy spans and references have no direct equivalent on `ak::Array`.
Use a read-only exact-type `array.view<T>()`, checked access, `ak::to_list`, or
low-level immutable layouts according to the required access pattern.

## Behavioral scope

The compatibility API intentionally favors Awkward-style behavior over exact
source compatibility with `jagged::Array<T>`. Current limits—including
byte-oriented string transforms and heterogeneous value-tree reconstruction—are
tracked in the [compatibility matrix](awkward-compatibility.md).
