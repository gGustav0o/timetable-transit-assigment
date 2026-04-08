from __future__ import annotations

import math
from collections import defaultdict

from demand_generator.types import (
    DailyOdDemand,
    DemandEntryRow,
    DiagnosticsSummary,
    ParsedSupply,
    PipelineResult,
    StructuralModel,
    TemporalShare,
    TimeIntervalRow,
)


def validate_parsed_supply(supply: ParsedSupply) -> ParsedSupply:
    if not supply.rows:
        raise ValueError("parsed supply must contain at least one row")
    for index, row in enumerate(supply.rows, start=1):
        validate_finite_non_negative(float(row.travel_time_sec), f"supply[{index}].travel_time_sec")
        validate_finite_non_negative(float(row.length), f"supply[{index}].length")
        validate_finite_non_negative(float(row.fare), f"supply[{index}].fare")
        if row.dep_sec is not None:
            validate_finite_scalar(float(row.dep_sec), f"supply[{index}].dep_sec")
        if row.arr_sec is not None:
            validate_finite_scalar(float(row.arr_sec), f"supply[{index}].arr_sec")
    return supply


def validate_structural_model(structure: StructuralModel) -> StructuralModel:
    if not structure.zone_ids:
        raise ValueError("structural model must contain at least one zone")
    if tuple(sorted(structure.zone_ids)) != structure.zone_ids:
        raise ValueError("structural model zone_ids must be sorted")
    if len(set(structure.zone_ids)) != len(structure.zone_ids):
        raise ValueError("structural model zone_ids must be unique")

    zone_set = set(structure.zone_ids)
    for zone in structure.zones:
        if zone.zone_id not in zone_set:
            raise ValueError(f"zone structure references unknown zone {int(zone.zone_id)}")
        validate_zone_mass(zone)

    for pair in structure.pair_connectivity:
        if pair.origin_zone_id == pair.destination_zone_id:
            raise ValueError("pair connectivity must not contain intrazonal pairs")
        if pair.origin_zone_id not in zone_set or pair.destination_zone_id not in zone_set:
            raise ValueError("pair connectivity references unknown zone")
        validate_finite_in_closed_unit_interval(float(pair.decay), "pair_connectivity.decay")

    return structure


def validate_time_intervals(intervals: tuple[TimeIntervalRow, ...]) -> tuple[TimeIntervalRow, ...]:
    if not intervals:
        raise ValueError("time intervals must be non-empty")
    expected_interval_id = 1
    previous_end = None
    for interval in intervals:
        if int(interval.interval_id) != expected_interval_id:
            raise ValueError("interval ids must be contiguous and start from 1")
        if int(interval.start_sec) >= int(interval.end_sec):
            raise ValueError("each interval must satisfy start_sec < end_sec")
        if previous_end is not None and int(interval.start_sec) != previous_end:
            raise ValueError("intervals must be contiguous and non-overlapping")
        previous_end = int(interval.end_sec)
        expected_interval_id += 1
    return intervals


def validate_daily_od_demand(
    daily_demand: tuple[DailyOdDemand, ...],
) -> tuple[DailyOdDemand, ...]:
    seen_pairs: set[tuple[object, object]] = set()
    for entry in daily_demand:
        key = (entry.origin_zone_id, entry.destination_zone_id)
        if key in seen_pairs:
            raise ValueError("daily demand must not contain duplicate OD pairs")
        seen_pairs.add(key)
        validate_finite_non_negative(float(entry.passengers), "daily_demand.passengers")
    return daily_demand


def validate_temporal_shares(
    temporal_shares: tuple[TemporalShare, ...],
) -> tuple[TemporalShare, ...]:
    if not temporal_shares:
        raise ValueError("temporal shares must be non-empty")
    sums: dict[tuple[object, object], float] = defaultdict(float)
    for share in temporal_shares:
        validate_finite_in_closed_unit_interval(float(share.share), "temporal_share.share")
        sums[(share.origin_zone_id, share.destination_zone_id)] += float(share.share)
    for key, total in sums.items():
        if abs(total - 1.0) > 1e-9:
            raise ValueError(f"temporal shares for OD {key} must sum to 1, got {total}")
    return temporal_shares


def validate_demand_entries(
    demand_entries: tuple[DemandEntryRow, ...],
    daily_demand: tuple[DailyOdDemand, ...] | None = None,
) -> tuple[DemandEntryRow, ...]:
    for entry in demand_entries:
        validate_finite_non_negative(float(entry.passengers), "demand_entry.passengers")

    if daily_demand is None:
        return demand_entries

    expected_by_pair = {
        (entry.origin_zone_id, entry.destination_zone_id): float(entry.passengers)
        for entry in daily_demand
    }
    observed_by_pair: dict[tuple[object, object], float] = defaultdict(float)
    for entry in demand_entries:
        observed_by_pair[(entry.origin_zone_id, entry.destination_zone_id)] += float(entry.passengers)

    for key, expected in expected_by_pair.items():
        observed = observed_by_pair.get(key, 0.0)
        if abs(observed - expected) > 1e-6:
            raise ValueError(
                f"demand entries for OD {key} must conserve daily demand, "
                f"expected {expected}, got {observed}"
            )
    return demand_entries


def validate_pipeline_result(result: PipelineResult) -> PipelineResult:
    validate_structural_model(result.structure)

    if result.synthesis.structure != result.structure:
        raise ValueError("synthesis.structure must be identical to pipeline structure")

    validate_time_intervals(result.synthesis.intervals)
    validate_daily_od_demand(result.synthesis.daily_demand)
    validate_temporal_shares(result.synthesis.temporal_shares)
    validate_demand_entries(
        result.synthesis.demand_entries,
        daily_demand=result.synthesis.daily_demand,
    )
    validate_filtered_written_demand_entries(
        demand_entries=result.synthesis.demand_entries,
        written_demand_entries=result.written_demand_entries,
    )
    validate_diagnostics_summary(
        summary=result.diagnostics,
        result=result,
    )
    return result


def validate_filtered_written_demand_entries(
    demand_entries: tuple[DemandEntryRow, ...],
    written_demand_entries: tuple[DemandEntryRow, ...],
) -> None:
    validate_demand_entries(written_demand_entries)
    demand_entry_set = set(demand_entries)
    if any(entry not in demand_entry_set for entry in written_demand_entries):
        raise ValueError("written demand entries must be a subset of synthesized demand entries")


def validate_diagnostics_summary(
    summary: DiagnosticsSummary,
    result: PipelineResult,
) -> None:
    if summary.input_row_count != len(result.structure.supply.rows):
        raise ValueError("diagnostics input_row_count does not match pipeline structure")
    if summary.zone_count != len(result.structure.zone_ids):
        raise ValueError("diagnostics zone_count does not match pipeline structure")
    if summary.zone_stop_link_count != len(result.structure.zone_stop_links):
        raise ValueError("diagnostics zone_stop_link_count does not match pipeline structure")
    if summary.stop_stop_link_count != len(result.structure.stop_stop_links):
        raise ValueError("diagnostics stop_stop_link_count does not match pipeline structure")
    if summary.pair_connectivity_count != len(result.structure.pair_connectivity):
        raise ValueError("diagnostics pair_connectivity_count does not match pipeline structure")
    if summary.interval_count != len(result.synthesis.intervals):
        raise ValueError("diagnostics interval_count does not match synthesis intervals")
    if summary.od_pair_count != len(result.synthesis.daily_demand):
        raise ValueError("diagnostics od_pair_count does not match daily demand")
    if abs(float(summary.total_daily_demand) - sum(float(entry.passengers) for entry in result.synthesis.daily_demand)) > 1e-6:
        raise ValueError("diagnostics total_daily_demand does not match synthesized daily demand")
    if abs(float(summary.total_interval_demand) - sum(float(entry.passengers) for entry in result.synthesis.demand_entries)) > 1e-6:
        raise ValueError("diagnostics total_interval_demand does not match synthesized demand entries")
    expected_filtered = len(result.synthesis.demand_entries) - len(result.written_demand_entries)
    if summary.filtered_out_demand_row_count != expected_filtered:
        raise ValueError("diagnostics filtered_out_demand_row_count does not match output filtering")
    if summary.written_demand_row_count != len(result.written_demand_entries):
        raise ValueError("diagnostics written_demand_row_count does not match output filtering")


def validate_zone_mass(zone: object) -> None:
    mass = zone.mass
    validate_finite_non_negative(float(mass.base_mass), "zone.mass.base_mass")
    validate_finite_non_negative(float(mass.derived_mass), "zone.mass.derived_mass")
    validate_finite_non_negative(float(mass.total_mass), "zone.mass.total_mass")
    if abs(float(mass.total_mass) - (float(mass.base_mass) + float(mass.derived_mass))) > 1e-9:
        raise ValueError("zone total_mass must equal base_mass + derived_mass")


def validate_finite_non_negative(value: float, label: str) -> None:
    validate_finite_scalar(value, label)
    if value < 0.0:
        raise ValueError(f"{label} must be non-negative, got {value}")


def validate_finite_in_closed_unit_interval(value: float, label: str) -> None:
    validate_finite_scalar(value, label)
    if value < 0.0 or value > 1.0:
        raise ValueError(f"{label} must lie in [0, 1], got {value}")


def validate_finite_scalar(value: float, label: str) -> None:
    if not math.isfinite(value):
        raise ValueError(f"{label} must be finite, got {value}")
