from __future__ import annotations

from demand_generator.config import GeneratorConfig
from demand_generator.csv_input import load_parsed_supply
from demand_generator.csv_output import select_written_demand_entries
from demand_generator.demand_model import build_daily_od_demand
from demand_generator.diagnostics import build_diagnostics_summary
from demand_generator.network_features import build_structural_model
from demand_generator.split import (
    build_temporal_shares_for_daily_demand,
    split_daily_demand,
)
from demand_generator.time_intervals import build_time_intervals, resolve_modeling_time_range
from demand_generator.types import PipelineResult, SynthesisModel
from demand_generator.validation import validate_pipeline_result


def run_generation_pipeline(config: GeneratorConfig) -> PipelineResult:
    parsed_supply = load_parsed_supply(config.input_path)
    structure = build_structural_model(
        supply=parsed_supply,
        zone_mass_strategy=config.strategies.zone_mass,
        decay_strategy=config.strategies.connectivity_decay,
    )

    modeling_time_range = resolve_modeling_time_range(
        config=config,
        inferred_time_range=structure.inferred_time_range,
    )
    intervals = build_time_intervals(
        modeling_time_range=modeling_time_range,
        interval_sec=config.interval_sec,
    )

    daily_demand = build_daily_od_demand(
        structure=structure,
        seed=config.seed,
        strategy=config.strategies.daily_demand,
        include_intrazonal=config.include_intrazonal,
    )
    temporal_shares = build_temporal_shares_for_daily_demand(
        daily_demand=daily_demand,
        intervals=intervals,
        seed=config.seed,
        strategy=config.strategies.temporal_profile,
    )
    demand_entries = split_daily_demand(
        daily_demand=daily_demand,
        temporal_shares=temporal_shares,
    )
    written_demand_entries = select_written_demand_entries(
        demand_entries=demand_entries,
        sparse_od_threshold=config.sparse_od_threshold,
        max_written_od_pairs=config.max_written_od_pairs,
        max_written_demand_rows=config.max_written_demand_rows,
    )

    synthesis = SynthesisModel(
        structure=structure,
        modeling_time_range=modeling_time_range,
        intervals=intervals,
        daily_demand=daily_demand,
        temporal_shares=temporal_shares,
        demand_entries=demand_entries,
    )
    diagnostics = build_diagnostics_summary(
        structure=structure,
        modeling_time_range=modeling_time_range,
        intervals=intervals,
        daily_demand=daily_demand,
        demand_entries=demand_entries,
        written_demand_entries=written_demand_entries,
    )

    return validate_pipeline_result(PipelineResult(
        structure=structure,
        synthesis=synthesis,
        written_demand_entries=written_demand_entries,
        diagnostics=diagnostics,
    ))
