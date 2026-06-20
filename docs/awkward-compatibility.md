# Awkward Compatibility Matrix

This document tracks the planned compatibility surface for the Awkward-style `ak::*`
API. It is intentionally tied to one Python Awkward release so generated goldens
stay reproducible.

## Version Pin

- Python Awkward reference version: `2.9.1`
- Golden generator: `scripts/generate_awkward_goldens.py`
- Default generator behavior: fail if the installed `awkward.__version__` is not
  exactly `2.9.1`, unless `--allow-version-mismatch` is passed for exploratory
  local work.

Generate the default fixture set with:

```bash
python scripts/generate_awkward_goldens.py --output-dir build/awkward-goldens
```

When CMake is configured with `-DJAGGEDCPP_AWKWARD_GOLDEN_TESTS=ON`, CTest runs
the same generator into the build tree. The option requires Python and Awkward
`2.9.1`; the main C++ build remains dependency-free.

## Status Legend

- `implemented`: available in the compatibility `ak::*` API.
- `planned`: in scope for an upcoming compatibility milestone.
- `deferred`: intentionally out of the first compatibility milestone.
- `impossible-in-c++`: Python syntax or runtime behavior that cannot map directly
  to an idiomatic C++ API.

The compatibility `ak::*` API supports primitive arrays, list-offset arrays, regular arrays, empty arrays, and simple layout operations. The existing `jagged::Array<T>` remains a legacy seed for additional operations until compatibility entirely replaces that surface.

Option/mask layout support is available for missing values, scoped to primitive arrays, one-level list arrays, and option content nested inside those lists. Reducers, statistics, sorting, argsort, softmax, and `nan*` reducer variants are supported for flat and one-level list layouts. Reducer results use `ak::Value` until a dedicated high-level scalar object lands; array-shaped operations return `ak::Array`.

Record support covers `RecordArray`, `ak::Record`, ordered named fields, tuple records, field projection, `ak::zip`, `ak::zip_no_broadcast`, `ak::unzip`, `ak::with_field`, `ak::without_field`, and `ak::with_name` for top-level records and records inside matching one-level lists.

Value-tree broadcasting and elementwise operations cover scalar-array broadcasting, matching ragged structures, option propagation, field-compatible records, `ak::where`, `ak::isclose`, `ak::array_equal`, `ak::almost_equal`, and like-shaped constructors.

C++ buffer/form interop provides deterministic packed output with stable node keys and typed buffer containers via `ak::to_buffers`, reconstructs arrays from `ak::Form`, length, and `ak::BufferMap` via `ak::from_buffers`, and supports deterministic JSON persistence.

Append-only `ak::ArrayBuilder` constructs nulls, primitive values, lists, tuples, records, and nested combinations with heterogeneous output represented by `UnionArray`.

Strings are stored as offset-indexed UTF-8 byte buffers. Core `ak::str::*` predicates, case transforms, slicing, splitting, joining, and substring tests recurse through nested lists and options, currently with ASCII behavior.

The native API includes concrete type/scalar objects, placeholders, generic regular content, concatenation, string buffering, union reconstruction, strict versioned JSON/binary buffer persistence, typed access, and sanitizer/concurrent-read verification.

## Packaging

The installed package remains `jaggedcpp` to avoid breaking downstream
`find_package(jaggedcpp CONFIG REQUIRED)` calls. New consumers should link
`awkward::awkward`; the existing `jagged::jagged` target remains available.
Both targets expose the header-only compatibility API. See the
[migration guide](migrating-from-jagged-array.md) for legacy API mappings and
known behavioral differences.

## High-Level API Matrix

| API | Status | Python reference expression | Notes |
| --- | --- | --- | --- |
| `ak::Array` | implemented | `ak.Array([[1, 2, 3], [], [4, 5]])` | Primary high-level array wrapper around immutable layouts. |
| `ak::Record` | implemented | `ak.Array([{"x": 1, "y": [2, 3]}])[0]` | Scalar record view returned through `ak::Array::record_at(...)` for record values. |
| `ak::from_iter` | implemented | `ak.from_iter([[1, 2], [], [3]])` | C++ inputs replace Python iterables where type inference is unambiguous. Current support covers flat and one-level nested primitive/string-like inputs, plus `std::optional<T>` and `ak::Option<T>` values for missing primitive/list elements. |
| `ak::to_list` | implemented | `ak.to_list(ak.Array([[1, 2], [], [3]]))` | Canonical golden comparison output through `ak::Value`. |
| `ak::num` | implemented | `ak.num(ak.Array([[1, 2], [], [3]]), axis=1)` | The original vector-returning convenience remains; an axis overload supports recursive positive and negative axes through `ak::Value`. |
| `ak::flatten` | implemented | `ak.flatten(ak.Array([[1, 2], [], [3]]), axis=1)` | Supports removing a selected positive or negative recursive list axis. |
| `ak::ravel` | implemented | `ak.ravel(ak.Array([[1, 2], [], [3]]))` | Recursively removes all list dimensions while preserving scalar missing values. |
| `ak::unflatten` | implemented | `ak.unflatten(ak.Array([1, 2, 3, 4, 5]), [2, 0, 3])` | Validates counts against flat length. |
| `ak::to_packed` | implemented | `ak.to_packed(ak.Array([[1, 2], [], [3]]))` | No-op copy for currently packed initial layouts. |
| `ak::is_valid` | implemented | `ak.is_valid(ak.Array([[1, 2], [], [3]]))` | Boolean validity check for layouts. |
| `ak::validity_error` | implemented | `ak.validity_error(ak.Array([[1, 2], [], [3]]))` | Empty string means valid, matching Awkward. |
| indexing and slicing | implemented | `ak.Array([[10, 11], [], [12, 13, 14]])[:, :1]` | C++ uses explicit `ak::index::*` objects. Checked indexing supports row and nested integer/range/integer-array selection, ellipsis, new axis, field lists, and flat/ragged boolean masks uniformly across implemented layouts, including mixed nested array-backed masks. |
| `ak::local_index` | implemented | `ak.local_index(ak.Array([[10, 11], [], [12]]), axis=1)` | Supports explicit positive/negative axes across recursive implemented layouts. |
| `ak::firsts` | implemented | `ak.firsts(ak.Array([[10, 11], [], [12]]), axis=1)` | Current support covers `axis=1` over implemented list-like layouts and returns option output for empty lists. |
| `ak::singletons` | implemented | `ak.singletons(ak.Array([1, None, 3]))` | Current support wraps present top-level values in singleton lists and missing values in empty lists. |
| `ak::argsort` | implemented | `ak.argsort(ak.Array([[3, 1], [], [2]]), axis=1)` | Stable ordering supports recursive scalar-list axes, ragged column-wise axis 0, and `ascending` through `ak::SortOptions`. |
| `ak::argmin` | implemented | `ak.argmin(ak.Array([[3, 1], [], [2]]), axis=1, keepdims=True, mask_identity=True)` | Returns `ak::Value` results across recursive scalar-list axes; empty lists produce `None`. |
| `ak::argmax` | implemented | `ak.argmax(ak.Array([[3, 1], [], [2]]), axis=1, keepdims=True, mask_identity=True)` | Returns `ak::Value` results across recursive scalar-list axes; empty lists produce `None`. |
| `ak::mask` | implemented | `ak.mask(ak.Array([1, 2, 3]), ak.Array([True, False, True]))` | Preserves shape for flat row masks and ragged boolean masks over implemented layouts. |
| `ak::is_none` | implemented | `ak.is_none(ak.Array([1, None, 3]), axis=0)` | Current support covers `axis=0`, `axis=1`, and `axis=-1` for implemented layouts. |
| `ak::drop_none` | implemented | `ak.drop_none(ak.Array([[1, None], None, [2]]), axis=None)` | Supports recursive drop with `axis=None`, plus targeted `axis=0` or `axis=1` for implemented layouts. |
| `ak::pad_none` | implemented | `ak.pad_none(ak.Array([[1], [2, 3], []]), 3, axis=1, clip=False)` | Supports `axis=0/1` through `ak::PadNoneOptions`, including `clip=true`. |
| `ak::fill_none` | implemented | `ak.fill_none(ak.Array([1, None, 3]), 0)` | Supports scalar `ak::Value` replacement, including heterogeneous replacements represented by `UnionArray`. |
| `ak::nan_to_none` | implemented | `ak.nan_to_none(ak.Array([1.0, np.nan, 2.0]))` | Converts floating-point NaN values to option values in implemented layouts. |
| `ak::nan_to_num` | implemented | `ak.nan_to_num(ak.Array([1.0, np.nan, np.inf]), nan=0.0, posinf=9.0)` | Supports `nan`, `posinf`, and `neginf` replacement through `ak::NanToNumOptions`. |
| `ak::count` | implemented | `ak.count(ak.Array([[1, None], [], [2]]), axis=1, mask_identity=True)` | Uses `ak::ReducerOptions`; supports recursive axes, options, `axis=None`, negative axes, `keepdims`, and `mask_identity`. |
| `ak::count_nonzero` | implemented | `ak.count_nonzero(ak.Array([[0, 1], [], [2]]), axis=1, mask_identity=True)` | Supports bool and numeric arrays in implemented layouts. |
| `ak::sum` | implemented | `ak.sum(ak.Array([[1, 2], [], [3]]), axis=1, mask_identity=True)` | Empty-list identity behavior is implemented for current layouts. |
| `ak::prod` | implemented | `ak.prod(ak.Array([[2, 3], [], [4]]), axis=1, mask_identity=True)` | Python name maps to C++ `prod`, not `product`. |
| `ak::any` | implemented | `ak.any(ak.Array([[False, True], [], [False]]), axis=1, mask_identity=True)` | Boolean/numeric truth reducer for implemented layouts. |
| `ak::all` | implemented | `ak.all(ak.Array([[True, True], [], [False]]), axis=1, mask_identity=True)` | Boolean/numeric truth reducer for implemented layouts. |
| `ak::min` | implemented | `ak.min(ak.Array([[3, 1], [], [2]]), axis=1, mask_identity=True)` | Supports `initial` through `ak::ReducerOptions`; empty lists without an identity produce `None`. |
| `ak::max` | implemented | `ak.max(ak.Array([[3, 1], [], [2]]), axis=1, mask_identity=True)` | Supports `initial` through `ak::ReducerOptions`; empty lists without an identity produce `None`. |
| `ak::moment` | implemented | `ak.moment(ak.Array([[1.0, 2.0], [], [3.0]]), 2, axis=1, mask_identity=True)` | Central moment over implemented numeric layouts with tolerance-tested output. |
| `ak::mean` | implemented | `ak.mean(ak.Array([[1.0, 2.0], [], [3.0]]), axis=1, mask_identity=True)` | Floating-point reducer over implemented numeric layouts. |
| `ak::var` | implemented | `ak.var(ak.Array([[1.0, 2.0], [], [3.0]]), axis=1, mask_identity=True)` | Supports `ddof` through `ak::StatisticOptions`. |
| `ak::std` | implemented | `ak.std(ak.Array([[1.0, 2.0], [], [3.0]]), axis=1, mask_identity=True)` | Supports `ddof` through `ak::StatisticOptions`. |
| `ak::ptp` | implemented | `ak.ptp(ak.Array([[1.0, 3.0], [], [2.0]]), axis=1, mask_identity=True)` | Range reducer over implemented numeric layouts. |
| `ak::softmax` | implemented | `ak.softmax(ak.Array([[1.0, 2.0], [], [3.0]]), axis=1)` | Numerically stable softmax across recursive scalar-list axes. |
| `ak::sort` | implemented | `ak.sort(ak.Array([[3, 1], [], [2]]), axis=1)` | Stable ordering across recursive scalar-list axes; `None` sorts after present values. |
| nan reducers | implemented | `ak.nanmean(ak.Array([[1.0, np.nan], [], [3.0]]), axis=1, mask_identity=True)` | Implements `nansum`, `nanprod`, `nanmin`, `nanmax`, `nanmean`, `nanvar`, `nanstd`, `nanargmin`, and `nanargmax` for implemented numeric layouts. |
| `ak::zip` | implemented | `ak.zip({"x": ak.Array([1, 2]), "y": ak.Array([[3], []])})` | Preserves field order. Matching list structures are zipped into records inside lists by default; `ZipOptions::depth_limit` can stop at top-level records. |
| `ak::zip_no_broadcast` | implemented | `ak.zip_no_broadcast({"x": ak.Array([1, 2]), "y": ak.Array([3, 4])})` | Requires equal outer lengths and creates top-level records without deeper list zipping. |
| `ak::unzip` | implemented | `ak.unzip(ak.Array([{"x": 1, "y": 2}, {"x": 3, "y": 4}]))` | Returns fields in stored order for records and tuples. |
| `ak::fields` | implemented | `ak.fields(ak.Array([{"x": 1, "y": 2}]))` | Field order is part of the contract; tuple fields use numeric string names. |
| `ak::with_field` | implemented | `ak.with_field(ak.Array([{"x": 1}, {"x": 2}]), ak.Array([10, 20]), "y")` | Preserves existing fields and appends or replaces named fields for matching top-level or one-level nested record structure. |
| `ak::without_field` | implemented | `ak.without_field(ak.Array([{"x": 1, "y": 2}]), "y")` | Throws `std::out_of_range` for missing fields. |
| `ak::with_name` | implemented | `ak.with_name(ak.Array([{"x": 1}]), "Point")` | Metadata placeholder stored on record layouts and reflected in current type strings. |
| record projection | implemented | `ak.Array([{"x": 1, "y": 2}, {"x": 3, "y": 4}])["x"]` | C++ uses `ak::field(array, "x")`, `array.field("x")`, or `array.slice({ak::index::field("x")})`. |
| `ak::broadcast_arrays` | implemented | `ak.broadcast_arrays(ak.Array([[1, 2], []]), 10)` | Supports arrays plus scalar `ak::Value` overloads over implemented scalar/list/option shapes, including flat column values broadcast into ragged rows. |
| `ak::broadcast_fields` | implemented | `ak.broadcast_fields(ak.Array([{"x": 1}]), ak.Array([{"x": 2}]))` | Aligns matching record fields, including field reordering; mismatched field sets throw. |
| elementwise operations | implemented | `ak.Array([[1, 2], []]) + 10` | C++ exposes named functions: `add`, `subtract`, `multiply`, `divide`, comparisons, and logical operations. Compatible primitive/list-offset/regular numeric forms use layout-native kernels. |
| `ak::where` | implemented | `ak.where(ak.Array([True, False, True]), ak.Array([1, 2, 3]), 0)` | Supports broadcasted boolean conditions with array or scalar branches; missing conditions produce missing output. |
| `ak::isclose` | implemented | `ak.isclose(ak.Array([1.0, 2.0]), ak.Array([1.0, 2.1]))` | Uses Python-like default tolerances through `ak::CloseOptions`. |
| `ak::almost_equal` | implemented | `ak.almost_equal(ak.Array([1.0]), ak.Array([1.0]))` | Returns scalar bool with recursive numeric tolerance support. |
| `ak::array_equal` | implemented | `ak.array_equal(ak.Array([1]), ak.Array([1]))` | Returns scalar bool with optional NaN equality. |
| `ak::zeros_like` | implemented | `ak.zeros_like(ak.Array([[1, 2], []]))` | Preserves current list/record/option structure for numeric and boolean values. |
| `ak::ones_like` | implemented | `ak.ones_like(ak.Array([[1, 2], []]))` | Preserves current list/record/option structure for numeric and boolean values. |
| `ak::full_like` | implemented | `ak.full_like(ak.Array([[1, 2], []]), 7)` | Preserves structure and fills present scalar positions with the supplied `ak::Value`. |
| `ak::concatenate` | implemented | `ak.concatenate([ak.Array([[1], [2]]), ak.Array([[3], [4]])], axis=1)` | Supports positive and negative axes across matching depths; compatible axis-0 forms use native concatenation, a single input retains layout identity, and heterogeneous axis-0 output uses `UnionArray`. |
| arithmetic/comparison operators | implemented | `ak.Array([1, 2]) + 10` | C++ `+`, `-`, `*`, `/`, comparisons, `&`, `|`, and `!` delegate to the named elementwise operations. |
| `ak::to_buffers` | implemented | `ak.to_buffers(ak.Array([[1, 2], [], [3]]))` | Emits `ak::ToBuffersResult` with recursive `ak::Form`, top-level length, and typed `ak::BufferMap`; output is packed and uses stable keys such as `node0-offsets`. |
| `ak::from_buffers` | implemented | `ak.from_buffers(*ak.to_buffers(ak.Array([[1, 2], [], [3]])))` | Reconstructs implemented layouts from `ak::Form`, length, and `ak::BufferMap`; validates buffer presence, lengths, offsets, and layout consistency. |
| `ak::to_binary` / `ak::buffers_from_binary` | implemented | N/A | Deterministically round-trips every C++ `Buffer` alternative through a versioned binary format with strict malformed-input checks. |
| typed access and reducer results | implemented | N/A | `Value::is<T>()`/`get<T>()`, read-only exact-type `Array::view<T>()`, and `ReducerResult` through `sum_result`/`mean_result` provide explicit native C++ access while legacy reducers retain `Value` returns. |
| `ak::ArrayBuilder` | implemented | `((b := ak.ArrayBuilder()).begin_list(), b.integer(1), b.end_list(), b.snapshot())[-1]` | C++ builder exposes explicit scalar append, list, tuple/index, record/field, and snapshot methods. Builder mutation requires external synchronization; snapshots are immutable. |
| string predicates | implemented | `ak.str.is_alpha(ak.Array(["abc", "123", None]))` | Includes `is_alpha`, `is_alnum`, `is_ascii`, `is_decimal`, `is_digit`, `is_numeric`, `is_space`, `is_printable`, `is_lower`, `is_upper`, and `is_title`; options and nested lists are preserved. Classification is currently ASCII-only. |
| string transforms | implemented | `ak.str.upper(ak.Array(["abc", None, "De"]))` | Includes `lower`, `upper`, `capitalize`, `title`, and `reverse`. Case conversion is ASCII-only and reversal is byte-oriented. |
| string slicing/splitting/joining | implemented | `ak.str.split_pattern(ak.Array(["a,b", None]), ",")` | Includes byte-level `slice`, literal `split_pattern` with `SplitOptions::max_splits`, and `join`; missing values propagate. |
| string containment | implemented | `ak.str.contains(ak.Array(["abc", None]), "b")` | Includes literal byte-level `contains`, `starts_with`, and `ends_with`. |

## Low-Level Layout Matrix

| Layout or type/form concept | Status | Python reference expression | Notes |
| --- | --- | --- | --- |
| `EmptyArray` | implemented | `ak.Array([]).layout` | Unknown/empty type behavior. |
| `NumpyArray<T>` | implemented | `ak.Array([1, 2, 3]).layout` | Primitive contiguous buffer node. |
| `StringArray` | implemented | `ak.Array(["one", None, "three"]).layout.content` | High-level strings are scalar values backed by list offsets plus UTF-8 `uint8` content and Awkward-style `string`/`char` form parameters. |
| `ListOffsetArray<Index>` | implemented | `ak.Array([[1, 2], [], [3]]).layout` | First target for current flat-offset container. |
| `RegularArray` | implemented | `ak.to_regular(ak.Array([[1, 2], [3, 4]])).layout` | Fixed-size nested lists through `ak::regular`. |
| `ListArray<Index>` | implemented | `ak.contents.ListArray(ak.index.Index64(np.array([0, 2])), ak.index.Index64(np.array([2, 3])), ak.contents.NumpyArray(np.array([1, 2, 3]))).to_typetracer()` | Checked starts/stops, selection, packing, local indexes, type/form metadata, and buffer round trips are implemented. |
| `IndexedArray<Index>` | implemented | `ak.contents.IndexedArray(ak.index.Index64(np.array([2, 0, 1])), ak.contents.NumpyArray(np.array([10, 20, 30]))).to_typetracer()` | Checked non-option indirection supports selection, packing, field delegation, nested access, and buffer round trips. |
| `IndexedOptionArray<Index>` | implemented | `ak.Array([1, None, 3]).layout` | Missing values with indexed content; C++ uses `std::ptrdiff_t` indexes in the first implementation. |
| `ByteMaskedArray` | implemented | `ak.mask(ak.Array([1, 2, 3]), ak.Array([True, False, True])).layout` | Masked option representation used by flat `ak::mask`. |
| `BitMaskedArray` | implemented | `ak.contents.BitMaskedArray(ak.index.IndexU8(np.array([0b00000101], dtype=np.uint8)), ak.contents.NumpyArray(np.array([1, 2, 3])), valid_when=True, length=3, lsb_order=True).to_typetracer()` | Compact mask representation with `valid_when`, explicit length, and bit-order controls. |
| `UnmaskedArray` | implemented | `ak.contents.UnmaskedArray(ak.contents.NumpyArray(np.array([1, 2, 3]))).to_typetracer()` | Option type without missing entries. |
| `RecordArray` | implemented | `ak.Array([{"x": 1, "y": 2}]).layout` | Ordered named records and tuple records for top-level arrays and matching one-level list contents. |
| `UnionArray<TagIndex, Index>` | implemented | `ak.Array([1, "two", 3.0]).layout` | Supports heterogeneous builder, concatenation, missing-value, and elementwise reconstruction with checked tags/indexes and deterministic buffers. |
| type objects | implemented | `ak.type(ak.Array([[1, 2], [], [3]]))` | Includes `UnknownType`, `NumpyType`, `ListType`, `RegularType`, `OptionType`, `RecordType`, `UnionType`, `ArrayType`, and `ScalarType`. |
| form objects | implemented | `ak.to_buffers(ak.Array([[1, 2], [], [3]]))[0]` | Recursive C++ form metadata supports all implemented layouts plus deterministic dependency-free JSON serialization/deserialization. |

## Deferred Or Impossible APIs

| API family | Status | Reason |
| --- | --- | --- |
| Python bindings | deferred | Out of the first compatibility milestone; the first target is a native C++ API. |
| GPU/backend switching | deferred | Requires backend abstractions and external integrations that would distract from core semantics. |
| JAX, Numba, TensorFlow, Torch, cuDF, ROOT, and Dask integrations | deferred | Integration surfaces depend on Python or external runtimes that are out of scope. |
| Full NumPy `__array_function__` and ufunc dispatch | deferred | C++ will provide named functions first; operator support is added only where unambiguous. |
| Dynamic behavior dictionaries and mixin classes | deferred | Python runtime customization has no direct C++ equivalent; metadata placeholders may be preserved. |
| Full AwkwardForth | deferred | Requires a separate virtual-machine/parser effort after layout and buffer semantics stabilize. |
| Python slice syntax such as `array[:, 0]` | impossible-in-c++ | C++ cannot overload Python's slice literal syntax; use explicit index/slice objects. |
| Python keyword-only call style | impossible-in-c++ | C++ uses option structs or overloads for arguments such as `axis`, `keepdims`, and `mask_identity`. |
