from __future__ import annotations

from dataclasses import dataclass, replace
from pathlib import Path

from demand_generator.demand_model import DEFAULT_DAILY_DEMAND_STRATEGY
from demand_generator.network_features import (
    DEFAULT_CONNECTIVITY_DECAY_STRATEGY,
    DEFAULT_ZONE_MASS_STRATEGY,
)
from demand_generator.temporal_profile import DEFAULT_TEMPORAL_PROFILE_STRATEGY
from demand_generator.types import RuntimeStrategies

DEFAULT_START_SEC = 21600
DEFAULT_END_SEC = 82800
DEFAULT_INTERVAL_SEC = 3600
DEFAULT_SEED = 42
DEFAULT_DEMAND_SCALE = 1.0
DEFAULT_INCLUDE_INTRAZONAL = False
DEFAULT_SPARSE_OD_THRESHOLD = 0.0
DEFAULT_MAX_WRITTEN_OD_PAIRS: int | None = None
DEFAULT_MAX_WRITTEN_DEMAND_ROWS: int | None = None
DEFAULT_AUTO_TIME_RANGE = False
DEFAULT_VERBOSE = False


@dataclass(frozen=True, slots=True)
class GeneratorConfig:
    input_path: Path
    output_dir: Path
    start_sec: int
    end_sec: int
    interval_sec: int
    seed: int
    demand_scale: float
    include_intrazonal: bool
    sparse_od_threshold: float
    max_written_od_pairs: int | None
    max_written_demand_rows: int | None
    auto_time_range: bool
    verbose: bool
    strategies: RuntimeStrategies


def build_runtime_strategies(demand_scale: float) -> RuntimeStrategies:
    return RuntimeStrategies(
        zone_mass=DEFAULT_ZONE_MASS_STRATEGY,
        connectivity_decay=DEFAULT_CONNECTIVITY_DECAY_STRATEGY,
        daily_demand=replace(DEFAULT_DAILY_DEMAND_STRATEGY, demand_scale=demand_scale),
        temporal_profile=DEFAULT_TEMPORAL_PROFILE_STRATEGY,
    )


def make_generator_config(
    *,
    input_path: Path,
    output_dir: Path,
    start_sec: int = DEFAULT_START_SEC,
    end_sec: int = DEFAULT_END_SEC,
    interval_sec: int = DEFAULT_INTERVAL_SEC,
    seed: int = DEFAULT_SEED,
    demand_scale: float = DEFAULT_DEMAND_SCALE,
    include_intrazonal: bool = DEFAULT_INCLUDE_INTRAZONAL,
    sparse_od_threshold: float = DEFAULT_SPARSE_OD_THRESHOLD,
    max_written_od_pairs: int | None = DEFAULT_MAX_WRITTEN_OD_PAIRS,
    max_written_demand_rows: int | None = DEFAULT_MAX_WRITTEN_DEMAND_ROWS,
    auto_time_range: bool = DEFAULT_AUTO_TIME_RANGE,
    verbose: bool = DEFAULT_VERBOSE,
    strategies: RuntimeStrategies | None = None,
) -> GeneratorConfig:
    normalized_input_path = normalize_runtime_path(input_path)
    normalized_output_dir = normalize_runtime_path(output_dir)
    config = GeneratorConfig(
        input_path=normalized_input_path,
        output_dir=normalized_output_dir,
        start_sec=start_sec,
        end_sec=end_sec,
        interval_sec=interval_sec,
        seed=seed,
        demand_scale=demand_scale,
        include_intrazonal=include_intrazonal,
        sparse_od_threshold=sparse_od_threshold,
        max_written_od_pairs=max_written_od_pairs,
        max_written_demand_rows=max_written_demand_rows,
        auto_time_range=auto_time_range,
        verbose=verbose,
        strategies=build_runtime_strategies(demand_scale) if strategies is None else strategies,
    )
    return validate_generator_config(config)


def validate_generator_config(config: GeneratorConfig) -> GeneratorConfig:
    if config.interval_sec <= 0:
        raise ValueError(f"interval_sec must be positive, got {config.interval_sec}")
    if config.demand_scale <= 0.0:
        raise ValueError(f"demand_scale must be positive, got {config.demand_scale}")
    if config.sparse_od_threshold < 0.0:
        raise ValueError(
            f"sparse_od_threshold must be non-negative, got {config.sparse_od_threshold}"
        )
    if config.max_written_od_pairs is not None and config.max_written_od_pairs <= 0:
        raise ValueError(
            "max_written_od_pairs must be positive when specified, got "
            f"{config.max_written_od_pairs}"
        )
    if config.max_written_demand_rows is not None and config.max_written_demand_rows <= 0:
        raise ValueError(
            "max_written_demand_rows must be positive when specified, got "
            f"{config.max_written_demand_rows}"
        )
    if not config.auto_time_range:
        validate_explicit_time_range(
            start_sec=config.start_sec,
            end_sec=config.end_sec,
            interval_sec=config.interval_sec,
        )
    return config


def validate_explicit_time_range(start_sec: int, end_sec: int, interval_sec: int) -> None:
    if start_sec >= end_sec:
        raise ValueError(f"start_sec must be less than end_sec, got {start_sec} >= {end_sec}")
    duration = end_sec - start_sec
    if duration % interval_sec != 0:
        raise ValueError(
            "explicit modeling period length must be divisible by interval_sec, got "
            f"duration={duration}, interval_sec={interval_sec}"
        )


def normalize_runtime_path(path: Path) -> Path:
    return path.expanduser().resolve(strict=False)
