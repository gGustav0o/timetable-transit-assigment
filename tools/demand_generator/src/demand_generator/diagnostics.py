from __future__ import annotations

from demand_generator.types import (
    DailyOdDemand,
    DemandEntryRow,
    DiagnosticsSummary,
    PassengerCount,
    StructuralModel,
    TimeIntervalRow,
    TimeRange,
)


def build_diagnostics_summary(
    structure: StructuralModel,
    modeling_time_range: TimeRange,
    intervals: tuple[TimeIntervalRow, ...],
    daily_demand: tuple[DailyOdDemand, ...] = (),
    demand_entries: tuple[DemandEntryRow, ...] = (),
    written_demand_entries: tuple[DemandEntryRow, ...] | None = None,
) -> DiagnosticsSummary:
    total_daily_demand = PassengerCount(sum(float(entry.passengers) for entry in daily_demand))
    total_interval_demand = PassengerCount(sum(float(entry.passengers) for entry in demand_entries))
    written_row_count = (
        len(demand_entries)
        if written_demand_entries is None
        else len(written_demand_entries)
    )
    filtered_out_row_count = len(demand_entries) - written_row_count

    return DiagnosticsSummary(
        input_row_count=len(structure.supply.rows),
        zone_count=len(structure.zone_ids),
        zone_stop_link_count=len(structure.zone_stop_links),
        stop_stop_link_count=len(structure.stop_stop_links),
        pair_connectivity_count=len(structure.pair_connectivity),
        inferred_time_range=structure.inferred_time_range,
        modeling_time_range=modeling_time_range,
        interval_count=len(intervals),
        od_pair_count=len(daily_demand),
        total_daily_demand=total_daily_demand,
        total_interval_demand=total_interval_demand,
        filtered_out_demand_row_count=filtered_out_row_count,
        written_demand_row_count=written_row_count,
    )


def render_diagnostics_summary(summary: DiagnosticsSummary) -> tuple[str, ...]:
    inferred_time_range = (
        "<none>"
        if summary.inferred_time_range is None
        else format_time_range(summary.inferred_time_range)
    )
    return (
        f"input_rows={summary.input_row_count}",
        f"zone_count={summary.zone_count}",
        f"zone_stop_link_count={summary.zone_stop_link_count}",
        f"stop_stop_link_count={summary.stop_stop_link_count}",
        f"pair_connectivity_count={summary.pair_connectivity_count}",
        f"inferred_time_range={inferred_time_range}",
        f"modeling_time_range={format_time_range(summary.modeling_time_range)}",
        f"interval_count={summary.interval_count}",
        f"od_pair_count={summary.od_pair_count}",
        f"total_daily_demand={float(summary.total_daily_demand)}",
        f"total_interval_demand={float(summary.total_interval_demand)}",
        f"filtered_out_demand_row_count={summary.filtered_out_demand_row_count}",
        f"written_demand_row_count={summary.written_demand_row_count}",
    )


def format_time_range(time_range: TimeRange) -> str:
    return f"{int(time_range.start_sec)}:{int(time_range.end_sec)}"
