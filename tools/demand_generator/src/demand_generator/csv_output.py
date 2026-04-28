from __future__ import annotations

import csv
import io
from pathlib import Path
from typing import Final

from demand_generator.types import DemandEntryRow, OutputPaths, PipelineResult, TimeIntervalRow

TIME_INTERVALS_FILENAME: Final[str] = "time_intervals.csv"
OD_DEMAND_FILENAME: Final[str] = "od_demand.csv"

TIME_INTERVALS_HEADER: Final[tuple[str, str, str]] = (
    "interval_id",
    "start_sec",
    "end_sec",
)

OD_DEMAND_HEADER: Final[tuple[str, str, str, str]] = (
    "origin_zone_id",
    "destination_zone_id",
    "interval_id",
    "passengers",
)

CSV_ENCODING: Final[str] = "utf-8"
CSV_NEWLINE: Final[str] = "\n"
PASSENGERS_FORMAT: Final[str] = ".12g"


def build_output_paths(output_dir: Path) -> OutputPaths:
    return OutputPaths(
        output_dir=output_dir,
        time_intervals_csv=output_dir / TIME_INTERVALS_FILENAME,
        od_demand_csv=output_dir / OD_DEMAND_FILENAME,
    )


def filter_sparse_demand_entries(
    demand_entries: tuple[DemandEntryRow, ...],
    sparse_od_threshold: float,
) -> tuple[DemandEntryRow, ...]:
    return tuple(
        entry
        for entry in demand_entries
        if float(entry.passengers) > sparse_od_threshold
    )


def select_written_demand_entries(
    demand_entries: tuple[DemandEntryRow, ...],
    sparse_od_threshold: float,
    max_written_od_pairs: int | None,
    max_written_demand_rows: int | None,
) -> tuple[DemandEntryRow, ...]:
    return limit_written_demand_rows(
        demand_entries=limit_written_od_pairs(
            demand_entries=filter_sparse_demand_entries(
                demand_entries=demand_entries,
                sparse_od_threshold=sparse_od_threshold,
            ),
            max_written_od_pairs=max_written_od_pairs,
        ),
        max_written_demand_rows=max_written_demand_rows,
    )


def limit_written_od_pairs(
    demand_entries: tuple[DemandEntryRow, ...],
    max_written_od_pairs: int | None,
) -> tuple[DemandEntryRow, ...]:
    if max_written_od_pairs is None:
        return demand_entries

    selected_pairs = select_top_od_pairs(
        demand_entries=demand_entries,
        max_written_od_pairs=max_written_od_pairs,
    )
    return tuple(
        entry
        for entry in demand_entries
        if demand_entry_pair(entry) in selected_pairs
    )


def select_top_od_pairs(
    demand_entries: tuple[DemandEntryRow, ...],
    max_written_od_pairs: int,
) -> frozenset[tuple[int, int]]:
    pair_totals: dict[tuple[int, int], float] = {}
    for entry in demand_entries:
        pair = demand_entry_pair(entry)
        pair_totals[pair] = pair_totals.get(pair, 0.0) + float(entry.passengers)

    ranked_pairs = sorted(
        pair_totals.items(),
        key=lambda item: (-item[1], item[0][0], item[0][1]),
    )
    return frozenset(pair for pair, _ in ranked_pairs[:max_written_od_pairs])


def limit_written_demand_rows(
    demand_entries: tuple[DemandEntryRow, ...],
    max_written_demand_rows: int | None,
) -> tuple[DemandEntryRow, ...]:
    if max_written_demand_rows is None or len(demand_entries) <= max_written_demand_rows:
        return demand_entries
    return tuple(
        sorted(
            demand_entries,
            key=lambda entry: (
                -float(entry.passengers),
                int(entry.origin_zone_id),
                int(entry.destination_zone_id),
                int(entry.interval_id),
            ),
        )[:max_written_demand_rows]
    )


def demand_entry_pair(entry: DemandEntryRow) -> tuple[int, int]:
    return (int(entry.origin_zone_id), int(entry.destination_zone_id))


def serialize_time_intervals_csv(intervals: tuple[TimeIntervalRow, ...]) -> str:
    buffer = io.StringIO()
    writer = csv.writer(buffer, lineterminator=CSV_NEWLINE)
    writer.writerow(TIME_INTERVALS_HEADER)
    for interval in sort_time_intervals(intervals):
        writer.writerow(time_interval_csv_row(interval))
    return buffer.getvalue()


def serialize_od_demand_csv(demand_entries: tuple[DemandEntryRow, ...]) -> str:
    buffer = io.StringIO()
    writer = csv.writer(buffer, lineterminator=CSV_NEWLINE)
    writer.writerow(OD_DEMAND_HEADER)
    for entry in sort_demand_entries(demand_entries):
        writer.writerow(demand_entry_csv_row(entry))
    return buffer.getvalue()


def write_pipeline_outputs(
    result: PipelineResult,
    output_dir: Path,
) -> OutputPaths:
    paths = build_output_paths(output_dir)
    ensure_output_dir(paths.output_dir)
    paths.time_intervals_csv.write_text(
        serialize_time_intervals_csv(result.synthesis.intervals),
        encoding=CSV_ENCODING,
        newline=CSV_NEWLINE,
    )
    paths.od_demand_csv.write_text(
        serialize_od_demand_csv(result.written_demand_entries),
        encoding=CSV_ENCODING,
        newline=CSV_NEWLINE,
    )
    return paths


def ensure_output_dir(output_dir: Path) -> None:
    output_dir.mkdir(parents=True, exist_ok=True)


def sort_time_intervals(intervals: tuple[TimeIntervalRow, ...]) -> tuple[TimeIntervalRow, ...]:
    return tuple(sorted(intervals, key=lambda interval: int(interval.interval_id)))


def sort_demand_entries(demand_entries: tuple[DemandEntryRow, ...]) -> tuple[DemandEntryRow, ...]:
    return tuple(sorted(
        demand_entries,
        key=lambda entry: (
            int(entry.origin_zone_id),
            int(entry.destination_zone_id),
            int(entry.interval_id),
        ),
    ))


def time_interval_csv_row(interval: TimeIntervalRow) -> tuple[int, int, int]:
    return (
        int(interval.interval_id),
        int(interval.start_sec),
        int(interval.end_sec),
    )


def demand_entry_csv_row(entry: DemandEntryRow) -> tuple[int, int, int, str]:
    return (
        int(entry.origin_zone_id),
        int(entry.destination_zone_id),
        int(entry.interval_id),
        format(float(entry.passengers), PASSENGERS_FORMAT),
    )


def output_contract_lines() -> tuple[str, ...]:
    return (
        f"time_intervals.filename={TIME_INTERVALS_FILENAME}",
        f"time_intervals.header={','.join(TIME_INTERVALS_HEADER)}",
        "time_intervals.sort=interval_id asc",
        f"od_demand.filename={OD_DEMAND_FILENAME}",
        f"od_demand.header={','.join(OD_DEMAND_HEADER)}",
        "od_demand.sort=origin_zone_id asc, destination_zone_id asc, interval_id asc",
        "od_demand.zero_rows=omitted after sparse threshold filtering",
        "od_demand.max_written_od_pairs=optional deterministic top-total-demand OD cap",
        "od_demand.max_written_demand_rows=optional deterministic top-demand-row cap",
        f"csv.encoding={CSV_ENCODING}",
        r"csv.newline=\n",
        f"csv.passengers_format={PASSENGERS_FORMAT}",
    )
