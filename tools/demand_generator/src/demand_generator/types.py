from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
from typing import NewType


ZoneId = NewType("ZoneId", int)
StopId = NewType("StopId", int)
TripId = NewType("TripId", int)
LineId = NewType("LineId", int)
IntervalId = NewType("IntervalId", int)

Seconds = NewType("Seconds", int)
Distance = NewType("Distance", float)
PassengerCount = NewType("PassengerCount", float)
Mass = NewType("Mass", float)
Weight = NewType("Weight", float)


@dataclass(frozen=True, slots=True)
class SupplyRow:
    from_stop_id: StopId | None
    to_stop_id: StopId | None
    travel_time_sec: float
    length: Distance
    from_zone_id: ZoneId | None
    to_zone_id: ZoneId | None
    fare: float
    trip_id: TripId | None
    line_id: LineId | None
    from_index: int | None
    dep_sec: float | None
    to_index: int | None
    arr_sec: float | None


@dataclass(frozen=True, slots=True)
class ParsedSupply:
    rows: tuple[SupplyRow, ...]


@dataclass(frozen=True, slots=True)
class TimeRange:
    start_sec: Seconds
    end_sec: Seconds


@dataclass(frozen=True, slots=True)
class ZoneStopLink:
    zone_id: ZoneId
    stop_id: StopId


@dataclass(frozen=True, slots=True)
class StopStopLink:
    from_stop_id: StopId
    to_stop_id: StopId


@dataclass(frozen=True, slots=True)
class ZoneMassComponents:
    base_mass: Mass
    derived_mass: Mass
    total_mass: Mass
    incident_access_count: int
    unique_stop_count: int
    endpoint_segment_count: int


@dataclass(frozen=True, slots=True)
class ZoneMassStrategy:
    base_mass: Mass
    incident_access_weight: float
    unique_stop_weight: float
    endpoint_segment_weight: float


@dataclass(frozen=True, slots=True)
class ZoneStructure:
    zone_id: ZoneId
    connected_stop_ids: tuple[StopId, ...]
    mass: ZoneMassComponents


@dataclass(frozen=True, slots=True)
class PairConnectivity:
    origin_zone_id: ZoneId
    destination_zone_id: ZoneId
    decay: Weight
    direct_link_count: int
    shared_stop_count: int


@dataclass(frozen=True, slots=True)
class ConnectivityDecayStrategy:
    min_decay: Weight
    direct_link_weight: float
    shared_stop_weight: float


@dataclass(frozen=True, slots=True)
class NoiseRange:
    low: float
    high: float


@dataclass(frozen=True, slots=True)
class TemporalProfileStrategy:
    morning_peak_center_sec: Seconds
    morning_peak_width_sec: float
    morning_peak_height: float
    evening_peak_center_sec: Seconds
    evening_peak_width_sec: float
    evening_peak_height: float
    midday_plateau_height: float
    base_level: float
    variation_noise_range: NoiseRange


@dataclass(frozen=True, slots=True)
class DailyDemandStrategy:
    demand_scale: float
    min_passengers: PassengerCount


@dataclass(frozen=True, slots=True)
class RuntimeStrategies:
    zone_mass: ZoneMassStrategy
    connectivity_decay: ConnectivityDecayStrategy
    daily_demand: DailyDemandStrategy
    temporal_profile: TemporalProfileStrategy


@dataclass(frozen=True, slots=True)
class StructuralModel:
    supply: ParsedSupply
    zone_ids: tuple[ZoneId, ...]
    inferred_time_range: TimeRange | None
    zone_stop_links: tuple[ZoneStopLink, ...]
    stop_stop_links: tuple[StopStopLink, ...]
    zones: tuple[ZoneStructure, ...]
    pair_connectivity: tuple[PairConnectivity, ...]


@dataclass(frozen=True, slots=True)
class TimeIntervalRow:
    interval_id: IntervalId
    start_sec: Seconds
    end_sec: Seconds


@dataclass(frozen=True, slots=True)
class DailyOdDemand:
    origin_zone_id: ZoneId
    destination_zone_id: ZoneId
    passengers: PassengerCount


@dataclass(frozen=True, slots=True)
class TemporalShare:
    origin_zone_id: ZoneId
    destination_zone_id: ZoneId
    interval_id: IntervalId
    share: Weight


@dataclass(frozen=True, slots=True)
class DemandEntryRow:
    origin_zone_id: ZoneId
    destination_zone_id: ZoneId
    interval_id: IntervalId
    passengers: PassengerCount


@dataclass(frozen=True, slots=True)
class SynthesisModel:
    structure: StructuralModel
    modeling_time_range: TimeRange
    intervals: tuple[TimeIntervalRow, ...]
    daily_demand: tuple[DailyOdDemand, ...]
    temporal_shares: tuple[TemporalShare, ...]
    demand_entries: tuple[DemandEntryRow, ...]


@dataclass(frozen=True, slots=True)
class DiagnosticsSummary:
    input_row_count: int
    zone_count: int
    zone_stop_link_count: int
    stop_stop_link_count: int
    pair_connectivity_count: int
    inferred_time_range: TimeRange | None
    modeling_time_range: TimeRange
    interval_count: int
    od_pair_count: int
    total_daily_demand: PassengerCount
    total_interval_demand: PassengerCount
    filtered_out_demand_row_count: int
    written_od_pair_count: int
    written_demand_row_count: int


@dataclass(frozen=True, slots=True)
class PipelineResult:
    structure: StructuralModel
    synthesis: SynthesisModel
    written_demand_entries: tuple[DemandEntryRow, ...]
    diagnostics: DiagnosticsSummary


@dataclass(frozen=True, slots=True)
class OutputPaths:
    output_dir: Path
    time_intervals_csv: Path
    od_demand_csv: Path


@dataclass(frozen=True, slots=True)
class ApplicationResult:
    pipeline: PipelineResult
    output_paths: OutputPaths
    report_lines: tuple[str, ...]


@dataclass(frozen=True, slots=True)
class GenerationArtifacts:
    structure: StructuralModel
    synthesis: SynthesisModel
    diagnostics: DiagnosticsSummary
