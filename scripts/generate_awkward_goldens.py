#!/usr/bin/env python3
"""Generate JSON fixtures from pinned Python Awkward expressions."""

from __future__ import annotations

import argparse
import json
import math
import pathlib
from dataclasses import dataclass
from typing import Any


EXPECTED_AWKWARD_VERSION = "2.9.1"


@dataclass(frozen=True)
class GoldenCase:
    case_id: str
    api: str
    expression: str
    include_buffers: bool = False


DEFAULT_CASES = (
    GoldenCase(
        "array_nested_int64",
        "ak.Array",
        "ak.Array([[1, 2, 3], [], [4, 5]])",
        include_buffers=True,
    ),
    GoldenCase(
        "to_list_nested_int64",
        "ak.to_list",
        "ak.to_list(ak.Array([[1, 2], [], [3]]))",
    ),
    GoldenCase(
        "num_axis1",
        "ak.num",
        "ak.num(ak.Array([[1, 2], [], [3]]), axis=1)",
    ),
    GoldenCase(
        "flatten_axis1",
        "ak.flatten",
        "ak.flatten(ak.Array([[1, 2], [], [3]]), axis=1)",
    ),
    GoldenCase(
        "ravel_nested",
        "ak.ravel",
        "ak.ravel(ak.Array([[1, 2], [], [3]]))",
    ),
    GoldenCase(
        "unflatten_counts",
        "ak.unflatten",
        "ak.unflatten(ak.Array([1, 2, 3, 4, 5]), [2, 0, 3])",
        include_buffers=True,
    ),
    GoldenCase(
        "option_fill_none",
        "ak.fill_none",
        "ak.fill_none(ak.Array([1, None, 3]), 0)",
        include_buffers=True,
    ),
    GoldenCase(
        "record_array",
        "ak.Record",
        'ak.Array([{"x": 1, "y": [2, 3]}, {"x": 4, "y": []}])',
        include_buffers=True,
    ),
    GoldenCase(
        "builder_nested_mixed",
        "ak.ArrayBuilder",
        "(lambda b: (b.begin_list(), b.integer(1), b.real(2.5), b.null(), "
        "b.end_list(), b.begin_record(), b.field('name'), b.string('point'), "
        "b.field('values'), b.begin_list(), b.boolean(True), b.integer(3), "
        "b.end_list(), b.end_record(), b.snapshot())[-1])(ak.ArrayBuilder())",
        include_buffers=True,
    ),
    GoldenCase(
        "strings_optional",
        "ak.Array",
        "ak.Array(['Alpha', None, 'two words', ''])",
        include_buffers=True,
    ),
    GoldenCase(
        "strings_nested_upper",
        "ak.str.upper",
        "ak.str.upper(ak.Array([['One', None], [], ['three']]))",
    ),
    GoldenCase(
        "strings_split_pattern",
        "ak.str.split_pattern",
        "ak.str.split_pattern(ak.Array(['a,b,c', None, '', 'x,,y']), ',')",
    ),
    GoldenCase(
        "concatenate_axis1",
        "ak.concatenate",
        "ak.concatenate([ak.Array([[1], [2, 3]]), ak.Array([[4, 5], [6]])], axis=1)",
        include_buffers=True,
    ),
    GoldenCase(
        "fill_none_mixed_union",
        "ak.fill_none",
        "ak.fill_none(ak.Array([1, None, 3]), 'missing')",
        include_buffers=True,
    ),
    GoldenCase(
        "deep_sum_axis2",
        "ak.sum",
        "ak.sum(ak.Array([[[3, 1], [2]], [[4, 0]]]), axis=2)",
    ),
    GoldenCase(
        "deep_sort_axis2",
        "ak.sort",
        "ak.sort(ak.Array([[[3, 1], [2]], [[4, 0]]]), axis=2)",
    ),
    GoldenCase(
        "newaxis_outer",
        "ak.Array.__getitem__",
        "ak.Array([1, 2, 3])[np.newaxis, :]",
    ),
)


def _import_awkward() -> tuple[Any, Any]:
    try:
        import awkward as ak
        import numpy as np
    except ImportError as error:
        raise SystemExit(
            "Python Awkward and NumPy are required. Install awkward=="
            f"{EXPECTED_AWKWARD_VERSION} to generate goldens."
        ) from error
    return ak, np


def _check_version(ak: Any, allow_mismatch: bool) -> None:
    installed = getattr(ak, "__version__", "unknown")
    if installed != EXPECTED_AWKWARD_VERSION and not allow_mismatch:
        raise SystemExit(
            "Awkward version mismatch: expected "
            f"{EXPECTED_AWKWARD_VERSION}, found {installed}. "
            "Pass --allow-version-mismatch only for exploratory local output."
        )


def _jsonable(value: Any, np: Any) -> Any:
    if value is None or isinstance(value, (bool, int, str)):
        return value
    if isinstance(value, float):
        if math.isnan(value):
            return {"__float__": "nan"}
        if math.isinf(value):
            return {"__float__": "inf" if value > 0 else "-inf"}
        return value
    if isinstance(value, np.generic):
        return _jsonable(value.item(), np)
    if isinstance(value, np.ndarray):
        return {
            "dtype": str(value.dtype),
            "shape": list(value.shape),
            "data": _jsonable(value.tolist(), np),
        }
    if isinstance(value, bytes):
        return {"__bytes__": list(value)}
    if isinstance(value, dict):
        return {str(key): _jsonable(value[key], np) for key in sorted(value, key=str)}
    if isinstance(value, (list, tuple)):
        return [_jsonable(item, np) for item in value]
    return str(value)


def _evaluate_expression(expression: str, ak: Any, np: Any) -> Any:
    globals_ = {"__builtins__": {}}
    locals_ = {"ak": ak, "np": np, "math": math}
    return eval(expression, globals_, locals_)  # noqa: S307 - local dev fixture expressions only.


def _to_list(value: Any, ak: Any, np: Any) -> Any:
    try:
        return _jsonable(ak.to_list(value), np)
    except Exception:
        return _jsonable(value, np)


def _type_string(value: Any, ak: Any) -> str:
    try:
        return str(ak.type(value))
    except Exception:
        return type(value).__name__


def _buffers(value: Any, ak: Any, np: Any) -> dict[str, Any]:
    form, length, container = ak.to_buffers(value)
    form_json = form.to_json() if hasattr(form, "to_json") else str(form)
    try:
        parsed_form: Any = json.loads(form_json)
    except json.JSONDecodeError:
        parsed_form = form_json
    return {
        "form": parsed_form,
        "length": int(length),
        "container": _jsonable(container, np),
    }


def _render_case(case: GoldenCase, ak: Any, np: Any) -> dict[str, Any]:
    value = _evaluate_expression(case.expression, ak, np)
    result = {
        "awkward_version": ak.__version__,
        "case_id": case.case_id,
        "api": case.api,
        "expression": case.expression,
        "to_list": _to_list(value, ak, np),
        "type": _type_string(value, ak),
    }
    if case.include_buffers:
        result["buffers"] = _buffers(value, ak, np)
    return result


def _parse_extra_expression(raw_expression: str) -> GoldenCase:
    if "=" not in raw_expression:
        raise argparse.ArgumentTypeError(
            "custom expressions must use the form case_id=python_expression"
        )
    case_id, expression = raw_expression.split("=", 1)
    case_id = case_id.strip()
    expression = expression.strip()
    if not case_id or not expression:
        raise argparse.ArgumentTypeError(
            "custom expressions must include a non-empty case id and expression"
        )
    return GoldenCase(case_id, "custom", expression)


def _write_json(path: pathlib.Path, data: Any) -> None:
    with path.open("w", encoding="utf-8") as stream:
        json.dump(data, stream, indent=2, sort_keys=True, allow_nan=False)
        stream.write("\n")


def _selected_cases(case_ids: list[str] | None) -> list[GoldenCase]:
    cases_by_id = {case.case_id: case for case in DEFAULT_CASES}
    if not case_ids:
        return list(DEFAULT_CASES)

    selected = []
    for case_id in case_ids:
        try:
            selected.append(cases_by_id[case_id])
        except KeyError as error:
            valid = ", ".join(sorted(cases_by_id))
            raise SystemExit(f"Unknown case '{case_id}'. Valid cases: {valid}") from error
    return selected


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--output-dir",
        type=pathlib.Path,
        help="Directory where JSON fixtures and manifest.json are written.",
    )
    parser.add_argument(
        "--case",
        action="append",
        dest="case_ids",
        help="Default case id to generate. May be passed more than once.",
    )
    parser.add_argument(
        "--expression",
        action="append",
        type=_parse_extra_expression,
        default=[],
        help="Additional expression as case_id=python_expression.",
    )
    parser.add_argument(
        "--allow-version-mismatch",
        action="store_true",
        help="Generate output even when awkward.__version__ differs from the pinned version.",
    )
    parser.add_argument(
        "--list-cases",
        action="store_true",
        help="List built-in case ids without importing Awkward.",
    )
    args = parser.parse_args(argv)

    if args.list_cases:
        for case in DEFAULT_CASES:
            print(f"{case.case_id}\t{case.api}\t{case.expression}")
        return 0

    if args.output_dir is None:
        parser.error("--output-dir is required unless --list-cases is used")

    cases = _selected_cases(args.case_ids)
    cases.extend(args.expression)

    ak, np = _import_awkward()
    _check_version(ak, args.allow_version_mismatch)

    args.output_dir.mkdir(parents=True, exist_ok=True)
    manifest = {
        "awkward_version": ak.__version__,
        "expected_awkward_version": EXPECTED_AWKWARD_VERSION,
        "cases": [],
    }

    for case in cases:
        fixture = _render_case(case, ak, np)
        fixture_path = args.output_dir / f"{case.case_id}.json"
        _write_json(fixture_path, fixture)
        manifest["cases"].append(
            {
                "case_id": case.case_id,
                "api": case.api,
                "expression": case.expression,
                "file": fixture_path.name,
            }
        )

    _write_json(args.output_dir / "manifest.json", manifest)
    print(f"Wrote {len(cases)} Awkward golden fixture(s) to {args.output_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
