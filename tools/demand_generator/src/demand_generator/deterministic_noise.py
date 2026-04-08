from __future__ import annotations

import hashlib
import struct
from typing import Final

from demand_generator.types import IntervalId, NoiseRange, Weight, ZoneId


UINT64_SCALE: Final[float] = float(1 << 64)
DEFAULT_DAILY_NOISE_RANGE = NoiseRange(low=0.9, high=1.1)
DEFAULT_PROFILE_NOISE_RANGE = NoiseRange(low=0.95, high=1.05)


def pair_noise(
    seed: int,
    origin_zone_id: ZoneId,
    destination_zone_id: ZoneId,
    noise_range: NoiseRange = DEFAULT_DAILY_NOISE_RANGE,
    tag: str = "daily_od",
) -> float:
    return hashed_float(
        seed=seed,
        noise_range=noise_range,
        components=(tag, int(origin_zone_id), int(destination_zone_id)),
    )


def interval_noise(
    seed: int,
    origin_zone_id: ZoneId,
    destination_zone_id: ZoneId,
    interval_id: IntervalId,
    noise_range: NoiseRange = DEFAULT_PROFILE_NOISE_RANGE,
    tag: str = "temporal_profile",
) -> float:
    return hashed_float(
        seed=seed,
        noise_range=noise_range,
        components=(tag, int(origin_zone_id), int(destination_zone_id), int(interval_id)),
    )


def hashed_weight(
    seed: int,
    noise_range: NoiseRange,
    components: tuple[str | int, ...],
) -> Weight:
    return Weight(hashed_float(seed=seed, noise_range=noise_range, components=components))


def hashed_float(
    seed: int,
    noise_range: NoiseRange,
    components: tuple[str | int, ...],
) -> float:
    ensure_valid_noise_range(noise_range)
    unit = unit_interval_hash(seed=seed, components=components)
    return affine_map(unit=unit, low=noise_range.low, high=noise_range.high)


def unit_interval_hash(seed: int, components: tuple[str | int, ...]) -> float:
    digest = hashlib.blake2b(
        encode_noise_key(seed=seed, components=components),
        digest_size=8,
    ).digest()
    value = struct.unpack(">Q", digest)[0]
    return (value + 0.5) / UINT64_SCALE


def encode_noise_key(seed: int, components: tuple[str | int, ...]) -> bytes:
    parts = [f"seed={seed}"]
    parts.extend(render_noise_component(component) for component in components)
    return "|".join(parts).encode("utf-8")


def render_noise_component(component: str | int) -> str:
    if isinstance(component, str):
        return f"s:{component}"
    return f"i:{component}"


def affine_map(unit: float, low: float, high: float) -> float:
    return low + (high - low) * unit


def ensure_valid_noise_range(noise_range: NoiseRange) -> None:
    if not (noise_range.low < noise_range.high):
        raise ValueError(
            f"noise range must satisfy low < high, got {noise_range.low} >= {noise_range.high}"
        )
