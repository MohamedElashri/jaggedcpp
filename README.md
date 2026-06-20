# jaggedcpp

`jaggedcpp` is a small header-only C++20 library for jagged arrays: arrays whose
rows can have different lengths. Its primary interface is the Awkward
Array-inspired `ak::*` API, built around immutable layouts, validated buffers,
checked access, and simple CMake packaging.

## Requirements

- C++20 compiler.
- CMake 3.16 or newer.
- No third-party runtime or test dependencies.

## Build And Test

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

Enable AddressSanitizer and UndefinedBehaviorSanitizer with GCC or Clang:

```bash
cmake -S . -B build-sanitized -DCMAKE_BUILD_TYPE=Debug -DJAGGEDCPP_ENABLE_SANITIZERS=ON
cmake --build build-sanitized
ctest --test-dir build-sanitized --output-on-failure
```

Awkward compatibility work is tracked in
[`docs/awkward-compatibility.md`](docs/awkward-compatibility.md). Optional
Python golden fixture generation is gated behind Awkward `2.9.1`:

```bash
python scripts/generate_awkward_goldens.py --output-dir build/awkward-goldens
cmake -S . -B build-golden -DJAGGEDCPP_AWKWARD_GOLDEN_TESTS=ON
ctest --test-dir build-golden --output-on-failure
```

The Makefile is a thin wrapper around the same workflow:

```bash
make
make test
```

Existing `jagged::Array<T>` users can follow the
[migration guide](docs/migrating-from-jagged-array.md) to move to the primary
`ak::*` API.

## Use In CMake

After installing:

```bash
cmake --install build --prefix /path/to/prefix
```

Use the package from another project:

```cmake
find_package(jaggedcpp CONFIG REQUIRED)
target_link_libraries(my_target PRIVATE awkward::awkward)
```

The package remains named `jaggedcpp`. The `jagged::jagged` target is retained
for existing consumers and exposes the same installed headers.

## Basic Example

```cpp
#include <awkward/awkward.hpp>

#include <iostream>
#include <vector>

int main() {
    const auto array = ak::from_iter({{1, 2, 3}, {}, {4, 5}, {6, 7}});

    std::cout << array.length() << '\n';
    std::cout << ak::to_list(array) << '\n';

    const auto values = ak::flatten(array);
    std::cout << ak::to_list(values) << '\n';

    const auto rebuilt = ak::unflatten(values, std::vector<std::size_t>{3, 0, 2, 2});
    std::cout << ak::to_list(rebuilt) << '\n';
}
```

## Missing Values

The compatibility API supports missing primitive values and recursively nested
missing values. Static `from_iter` construction covers the common flat and
one-level forms; use `ArrayBuilder` for deeper mixed input:

```cpp
#include <awkward/awkward.hpp>

auto array = ak::from_iter<int>({{1, ak::none}, {}, {ak::none, 4}});
auto mask = ak::is_none(array, 1);
auto dense = ak::fill_none(array, 0);
auto dropped = ak::drop_none(array);
auto padded = ak::pad_none(array, 3);

auto row_masked = ak::mask(
    ak::from_iter({{1, 2}, {3}, {4, 5}}),
    ak::from_iter({true, false, true}));
```

## Records

Records preserve field order and can be projected by field name:

```cpp
#include <awkward/awkward.hpp>

auto points = ak::zip({
    {"x", ak::from_iter({1, 2, 3})},
    {"y", ak::from_iter({10, 20, 30})},
});

auto xs = ak::field(points, "x");
auto with_labels = ak::with_field(points, ak::from_iter({"a", "b", "c"}), "label");
auto fields = ak::fields(with_labels); // {"x", "y", "label"}
```

When all zipped fields have matching one-level list structure, `ak::zip`
creates records inside those lists. Use `ak::ZipOptions{.depth_limit = 1}` or
`ak::zip_no_broadcast` to create top-level records that contain list fields.

## Broadcasting And Elementwise Operations

The compatibility API supports scalar and matching ragged broadcasting for the
implemented layouts:

```cpp
#include <awkward/awkward.hpp>

auto array = ak::from_iter({{1, 2}, {}, {3}});
auto shifted = ak::add(array, 10);               // [[11, 12], [], [13]]
auto scaled = ak::multiply(array, ak::from_iter({10, 20, 30}));
auto selected = ak::where(ak::greater(array, 1), array, 0);
auto close = ak::isclose(ak::from_iter({1.0, 2.0}), ak::from_iter({1.0, 2.1}));
auto zeros = ak::zeros_like(array);
```

Missing values propagate through elementwise operations. Record field
broadcasting is available when record fields match, including reordering fields
with `ak::broadcast_fields`.

## Buffers And Forms

Implemented layouts can be serialized to deterministic C++ form metadata plus
typed buffers and reconstructed without Python dependencies:

```cpp
#include <awkward/awkward.hpp>

auto array = ak::from_iter({{1, 2}, {}, {3}});
auto buffers = ak::to_buffers(array);
auto roundtrip = ak::from_buffers(buffers);

auto binary = ak::to_binary(buffers.buffers);
auto restored_buffers = ak::buffers_from_binary(binary);
```

Buffer keys are stable for a packed layout, such as `node0-offsets` and
`node1-data`. `ak::to_json(form)` and `ak::form_from_json(json)` provide a
deterministic dependency-free representation of the C++ form metadata. Form
JSON uses schema version 1 and rejects missing, duplicate, and unknown members.
The binary buffer format is also versioned and rejects malformed or trailing
input.

## Types And Metadata

`array.type()` and `ak::type(array)` return concrete recursive type objects;
`array.typestr()` remains the compact string form. `array.ndim()` reports the
layout depth, and checked scalar access can be wrapped as `ak::Scalar` through
`array.scalar_at(index)` or `ak::scalar(value)`.

Attrs, behavior placeholders, and named axes are immutable array metadata:

```cpp
auto array = ak::from_iter({{1, 2}, {}, {3}});
array = ak::with_attrs(array, {{"source", "example"}});
array = ak::with_named_axis(array, "rows", 0);
```

Metadata is propagated by free array operations. Multi-array operations merge
metadata and reject conflicting values instead of silently selecting one
input's metadata.

`ak::Value::is<T>()` and `get<T>()` provide checked access to its exact variant
alternative. A flat primitive layout can be read without copying through an
exact physical-type view:

```cpp
auto flat = ak::from_iter(std::vector<int>{1, 2, 3});
auto view = flat.view<int>();
for (int value : view) {
    // read-only access
}

ak::ReducerResult result = ak::sum_result(array);
if (const auto* scalar = std::get_if<ak::Scalar>(&result)) {
    // scalar reduction
} else {
    const auto& reduced = std::get<ak::Array>(result);
}
```

`ak::sum` and the existing reducers continue to return `ak::Value` for source
compatibility; `ak::sum_result` and `ak::mean_result` expose the explicit
`std::variant<ak::Scalar, ak::Array>` form.

## Incremental Building

`ak::ArrayBuilder` incrementally constructs values when a compile-time C++
container type cannot describe the input. Lists, tuples, and records must be
explicitly opened and closed:

```cpp
#include <awkward/awkward.hpp>

ak::ArrayBuilder builder;
builder.begin_record();
builder.field("name");
builder.string("point");
builder.field("coordinates");
builder.begin_list();
builder.real(1.5);
builder.real(2.5);
builder.end_list();
builder.end_record();

auto array = builder.snapshot();
```

Each snapshot owns an immutable layout and is unaffected by later appends to
the builder. A builder is mutable and requires external synchronization.

## Strings

Strings use Awkward-compatible offset and UTF-8 byte buffers with `string` and
`char` form parameters. String operations recurse through nested lists and
preserve missing values:

```cpp
#include <awkward/awkward.hpp>

auto words = ak::from_iter<std::string>({"One", "two words", "123"});
auto upper = ak::str::upper(words);
auto alphabetic = ak::str::is_alpha(words);
auto pieces = ak::str::split_pattern(words, " ");
auto rebuilt = ak::str::join(pieces, "-");
```

Phase 9 string classification, casing, slicing, and reversal are deterministic
byte-level operations: ASCII follows the corresponding Python string behavior,
while non-ASCII UTF-8 bytes are preserved by case transforms and rejected by
classification predicates other than `is_ascii`. Slicing and reversal can split
or reorder UTF-8 code units; Unicode code-point semantics remain future work.

## Legacy API

`jagged::Array<T>` remains available for existing code, but new development
should use `ak::Array` and the `ak::*` operations above. The compatibility API
supports immutable layout sharing, records, options, unions, broadcasting,
buffer interop, builders, and strings that are not represented by the legacy
container. See [Migrating from `jagged::Array<T>`](docs/migrating-from-jagged-array.md)
for API mappings and behavioral differences.

## Safety Contract

`ak::Array` shares an immutable `ak::Content` layout. Initial layout constructors validate offsets, regular sizes, and primitive content lengths. `ak::is_valid` and `ak::validity_error` expose the checked layout state.

`jagged::Array<T>` owns its data with standard containers. The public flat-storage constructor validates that offsets are non-empty, start at zero, are monotonic, and end at `values.size()`.

All `const` methods are reentrant and safe for concurrent reads of the same object. Mutating methods, including `ArrayBuilder` append methods and legacy `append_row`, `clear`, `reserve_values`, and `reserve_rows`, require external synchronization.

`values()` and `row()` return non-owning `std::span` views for non-`bool` element types. These views are invalidated by later mutation of the same array. `Array<bool>` supports value-copy APIs such as `to_rows`, `flatten`, and `at`, but does not expose spans because `std::vector<bool>` is not a contiguous `bool` buffer.

Bad construction input throws `std::invalid_argument`. Bad row or column indexing throws `std::out_of_range`.

## License

This project is licensed under the MIT License. See [LICENSE](LICENSE) for details.
