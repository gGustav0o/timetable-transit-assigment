from __future__ import annotations

import csv
from dataclasses import dataclass
from pathlib import Path
from typing import Callable, TypeVar

from demand_generator.types import (
    Distance,
    LineId,
    ParsedSupply,
    StopId,
    SupplyRow,
    TripId,
    ZoneId,
)
from demand_generator.validation import validate_parsed_supply

T = TypeVar("T")

REQUIRED_COLUMNS: tuple[str, ...] = (
    "FROM_STOP_ID",
    "TO_STOP_ID",
    "TIME",
    "LENGTH",
    "FROM_ZONE_ID",
    "TO_ZONE_ID",
    "FARE",
    "TRIP_ID",
    "LINE_ID",
    "FROM_INDEX",
    "DEP",
    "TO_INDEX",
    "ARR",
)


@dataclass(frozen=True, slots=True)
class RawSupplyRow:
    row_number: int
    fields: dict[str, str]


def load_parsed_supply(path: Path) -> ParsedSupply:
    raw_rows = read_raw_supply_rows(path)
    rows = tuple(parse_supply_row(raw_row) for raw_row in raw_rows)
    return validate_parsed_supply(ParsedSupply(rows=rows))


def read_raw_supply_rows(path: Path) -> tuple[RawSupplyRow, ...]:
    ensure_input_file_exists(path)
    with path.open("r", encoding="utf-8-sig", newline="") as handle:
        reader = csv.DictReader(handle)
        ensure_required_columns(path, tuple(reader.fieldnames or ()))
        return tuple(
            RawSupplyRow(
                row_number=index,
                fields=normalize_row_dict(raw_row),
            )
            for index, raw_row in enumerate(reader, start=2)
        )


def ensure_input_file_exists(path: Path) -> None:
    if not path.exists():
        raise ValueError(f"input csv does not exist: {path}")
    if not path.is_file():
        raise ValueError(f"input path is not a file: {path}")


def ensure_required_columns(path: Path, header: tuple[str | None, ...]) -> None:
    present = {column for column in header if column}
    missing = tuple(column for column in REQUIRED_COLUMNS if column not in present)
    if missing:
        rendered = ", ".join(missing)
        raise ValueError(f"input csv is missing required columns in {path}: {rendered}")


def normalize_row_dict(raw_row: dict[str | None, str | None]) -> dict[str, str]:
    return {
        key: (value.strip() if value is not None else "")
        for key, value in raw_row.items()
        if key
    }


def parse_supply_row(raw_row: RawSupplyRow) -> SupplyRow:
    return SupplyRow(
        from_stop_id=parse_optional_id(raw_row, "FROM_STOP_ID", StopId),
        to_stop_id=parse_optional_id(raw_row, "TO_STOP_ID", StopId),
        travel_time_sec=parse_required_float(raw_row, "TIME"),
        length=Distance(parse_required_float(raw_row, "LENGTH")),
        from_zone_id=parse_optional_id(raw_row, "FROM_ZONE_ID", ZoneId),
        to_zone_id=parse_optional_id(raw_row, "TO_ZONE_ID", ZoneId),
        fare=parse_required_float(raw_row, "FARE"),
        trip_id=parse_optional_id(raw_row, "TRIP_ID", TripId),
        line_id=parse_optional_id(raw_row, "LINE_ID", LineId),
        from_index=parse_optional_int(raw_row, "FROM_INDEX"),
        dep_sec=parse_optional_float(raw_row, "DEP"),
        to_index=parse_optional_int(raw_row, "TO_INDEX"),
        arr_sec=parse_optional_float(raw_row, "ARR"),
    )


def parse_optional_id(raw_row: RawSupplyRow, field: str, ctor: Callable[[int], T]) -> T | None:
    value = parse_optional_int(raw_row, field)
    return None if value is None else ctor(value)


def parse_optional_int(raw_row: RawSupplyRow, field: str) -> int | None:
    text = read_field(raw_row, field)
    if text == "" or text == "-1":
        return None
    try:
        return int(float(text))
    except ValueError as exc:
        raise invalid_field(raw_row, field, text, "expected integer or -1") from exc


def parse_required_float(raw_row: RawSupplyRow, field: str) -> float:
    text = read_field(raw_row, field)
    if text == "":
        raise invalid_field(raw_row, field, text, "required float is empty")
    try:
        return float(text)
    except ValueError as exc:
        raise invalid_field(raw_row, field, text, "expected float") from exc


def parse_optional_float(raw_row: RawSupplyRow, field: str) -> float | None:
    text = read_field(raw_row, field)
    if text == "" or text == "-1":
        return None
    try:
        return float(text)
    except ValueError as exc:
        raise invalid_field(raw_row, field, text, "expected float or -1") from exc


def read_field(raw_row: RawSupplyRow, field: str) -> str:
    try:
        return raw_row.fields[field]
    except KeyError as exc:
        raise invalid_field(raw_row, field, "", "field is absent from parsed row") from exc


def invalid_field(raw_row: RawSupplyRow, field: str, value: str, message: str) -> ValueError:
    return ValueError(
        f"invalid field at row {raw_row.row_number}, column {field}, value {value!r}: {message}"
    )
