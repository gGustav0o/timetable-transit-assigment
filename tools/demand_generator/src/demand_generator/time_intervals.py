from __future__ import annotations

from demand_generator.config import GeneratorConfig
from demand_generator.types import IntervalId, Seconds, TimeIntervalRow, TimeRange
from demand_generator.validation import validate_time_intervals


def resolve_modeling_time_range(
    config: GeneratorConfig,
    inferred_time_range: TimeRange | None,
) -> TimeRange:
    if config.auto_time_range:
        if inferred_time_range is None:
            raise ValueError("auto_time_range=true but no inferred time range is available")
        time_range = inferred_time_range
    else:
        time_range = TimeRange(
            start_sec=Seconds(config.start_sec),
            end_sec=Seconds(config.end_sec),
        )

    validate_time_range(time_range)
    validate_interval_divisibility(time_range, config.interval_sec)
    return time_range


def build_time_intervals(
    modeling_time_range: TimeRange,
    interval_sec: int,
) -> tuple[TimeIntervalRow, ...]:
    validate_time_range(modeling_time_range)
    validate_interval_length(interval_sec)
    validate_interval_divisibility(modeling_time_range, interval_sec)

    start_sec = int(modeling_time_range.start_sec)
    end_sec = int(modeling_time_range.end_sec)

    return validate_time_intervals(tuple(
        TimeIntervalRow(
            interval_id=IntervalId(index + 1),
            start_sec=Seconds(interval_start),
            end_sec=Seconds(interval_start + interval_sec),
        )
        for index, interval_start in enumerate(range(start_sec, end_sec, interval_sec))
    ))


def validate_time_range(time_range: TimeRange) -> None:
    if int(time_range.start_sec) >= int(time_range.end_sec):
        raise ValueError(
            "time range must satisfy start_sec < end_sec, got "
            f"{int(time_range.start_sec)} >= {int(time_range.end_sec)}"
        )


def validate_interval_length(interval_sec: int) -> None:
    if interval_sec <= 0:
        raise ValueError(f"interval_sec must be positive, got {interval_sec}")


def validate_interval_divisibility(time_range: TimeRange, interval_sec: int) -> None:
    validate_interval_length(interval_sec)
    duration = int(time_range.end_sec) - int(time_range.start_sec)
    if duration % interval_sec != 0:
        raise ValueError(
            "modeling period length must be divisible by interval_sec, got "
            f"duration={duration}, interval_sec={interval_sec}"
        )
