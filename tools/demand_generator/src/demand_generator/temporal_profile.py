from __future__ import annotations

import math

from demand_generator.deterministic_noise import (
    DEFAULT_PROFILE_NOISE_RANGE,
    interval_noise,
)
from demand_generator.types import (
    Seconds,
    TemporalProfileStrategy,
    TemporalShare,
    TimeIntervalRow,
    Weight,
    ZoneId,
)


DEFAULT_TEMPORAL_PROFILE_STRATEGY = TemporalProfileStrategy(
    morning_peak_center_sec=Seconds(8 * 3600 + 30 * 60),
    morning_peak_width_sec=75.0 * 60.0,
    morning_peak_height=0.9,
    evening_peak_center_sec=Seconds(18 * 3600),
    evening_peak_width_sec=90.0 * 60.0,
    evening_peak_height=1.0,
    midday_plateau_height=0.35,
    base_level=0.12,
    variation_noise_range=DEFAULT_PROFILE_NOISE_RANGE,
)


def build_temporal_profile_for_od(
    seed: int,
    origin_zone_id: ZoneId,
    destination_zone_id: ZoneId,
    intervals: tuple[TimeIntervalRow, ...],
    strategy: TemporalProfileStrategy = DEFAULT_TEMPORAL_PROFILE_STRATEGY,
) -> tuple[TemporalShare, ...]:
    validate_intervals(intervals)
    base_weights = tuple(
        base_interval_weight(interval=interval, strategy=strategy)
        for interval in intervals
    )
    varied_weights = tuple(
        vary_interval_weight(
            seed=seed,
            origin_zone_id=origin_zone_id,
            destination_zone_id=destination_zone_id,
            interval=interval,
            base_weight=base_weight,
            strategy=strategy,
        )
        for interval, base_weight in zip(intervals, base_weights)
    )
    normalized_weights = normalize_weights(varied_weights)
    return tuple(
        TemporalShare(
            origin_zone_id=origin_zone_id,
            destination_zone_id=destination_zone_id,
            interval_id=interval.interval_id,
            share=Weight(weight),
        )
        for interval, weight in zip(intervals, normalized_weights)
    )


def base_interval_weight(
    interval: TimeIntervalRow,
    strategy: TemporalProfileStrategy,
) -> float:
    midpoint_sec = interval_midpoint_sec(interval)
    morning_peak = gaussian_bump(
        x=midpoint_sec,
        center=float(strategy.morning_peak_center_sec),
        width=strategy.morning_peak_width_sec,
        height=strategy.morning_peak_height,
    )
    evening_peak = gaussian_bump(
        x=midpoint_sec,
        center=float(strategy.evening_peak_center_sec),
        width=strategy.evening_peak_width_sec,
        height=strategy.evening_peak_height,
    )
    midday_plateau = midday_plateau_weight(midpoint_sec)
    return (
        strategy.base_level
        + morning_peak
        + evening_peak
        + strategy.midday_plateau_height * midday_plateau
    )


def vary_interval_weight(
    seed: int,
    origin_zone_id: ZoneId,
    destination_zone_id: ZoneId,
    interval: TimeIntervalRow,
    base_weight: float,
    strategy: TemporalProfileStrategy,
) -> float:
    multiplier = interval_noise(
        seed=seed,
        origin_zone_id=origin_zone_id,
        destination_zone_id=destination_zone_id,
        interval_id=interval.interval_id,
        noise_range=strategy.variation_noise_range,
    )
    return base_weight * multiplier


def interval_midpoint_sec(interval: TimeIntervalRow) -> float:
    return 0.5 * (float(interval.start_sec) + float(interval.end_sec))


def gaussian_bump(x: float, center: float, width: float, height: float) -> float:
    if width <= 0.0:
        raise ValueError(f"gaussian width must be positive, got {width}")
    z = (x - center) / width
    return height * math.exp(-0.5 * z * z)


def midday_plateau_weight(midpoint_sec: float) -> float:
    midday_start = 11.0 * 3600.0
    midday_end = 15.0 * 3600.0
    ramp = 3600.0

    rise = smooth_step((midpoint_sec - (midday_start - ramp)) / ramp)
    fall = 1.0 - smooth_step((midpoint_sec - midday_end) / ramp)
    return clamp01(rise * fall)


def smooth_step(x: float) -> float:
    t = clamp01(x)
    return t * t * (3.0 - 2.0 * t)


def clamp01(x: float) -> float:
    return min(1.0, max(0.0, x))


def normalize_weights(weights: tuple[float, ...]) -> tuple[float, ...]:
    total = sum(weights)
    if not math.isfinite(total) or total <= 0.0:
        raise ValueError(f"weights must sum to a finite positive value, got {total}")
    normalized = tuple(weight / total for weight in weights)
    validate_normalized_weights(normalized)
    return normalized


def validate_normalized_weights(weights: tuple[float, ...]) -> None:
    if not weights:
        raise ValueError("temporal profile requires at least one interval")
    if any((not math.isfinite(weight)) or weight < 0.0 for weight in weights):
        raise ValueError("temporal profile weights must be finite and non-negative")
    total = sum(weights)
    if abs(total - 1.0) > 1e-9:
        raise ValueError(f"normalized weights must sum to 1, got {total}")


def validate_intervals(intervals: tuple[TimeIntervalRow, ...]) -> None:
    if not intervals:
        raise ValueError("temporal profile requires non-empty intervals")
