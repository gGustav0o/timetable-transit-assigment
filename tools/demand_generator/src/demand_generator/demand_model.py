from __future__ import annotations

import math

from demand_generator.deterministic_noise import pair_noise
from demand_generator.types import (
    DailyDemandStrategy,
    DailyOdDemand,
    PairConnectivity,
    PassengerCount,
    StructuralModel,
    ZoneId,
)
from demand_generator.validation import validate_daily_od_demand


DEFAULT_DAILY_DEMAND_STRATEGY = DailyDemandStrategy(
    demand_scale=1.0,
    min_passengers=PassengerCount(0.0),
)


def build_daily_od_demand(
    structure: StructuralModel,
    seed: int,
    strategy: DailyDemandStrategy = DEFAULT_DAILY_DEMAND_STRATEGY,
    include_intrazonal: bool = False,
) -> tuple[DailyOdDemand, ...]:
    zone_mass_by_id = {zone.zone_id: zone.mass for zone in structure.zones}
    connectivity_by_pair = {
        (pair.origin_zone_id, pair.destination_zone_id): pair
        for pair in structure.pair_connectivity
    }

    return validate_daily_od_demand(tuple(
        build_daily_od_entry(
            origin_zone_id=origin_zone_id,
            destination_zone_id=destination_zone_id,
            zone_mass_by_id=zone_mass_by_id,
            connectivity_by_pair=connectivity_by_pair,
            seed=seed,
            strategy=strategy,
        )
        for origin_zone_id in structure.zone_ids
        for destination_zone_id in structure.zone_ids
        if include_intrazonal or origin_zone_id != destination_zone_id
    ))


def build_daily_od_entry(
    origin_zone_id: ZoneId,
    destination_zone_id: ZoneId,
    zone_mass_by_id: dict[ZoneId, object],
    connectivity_by_pair: dict[tuple[ZoneId, ZoneId], PairConnectivity],
    seed: int,
    strategy: DailyDemandStrategy,
) -> DailyOdDemand:
    attraction_origin = attraction_from_total_mass(
        float(zone_mass_by_id[origin_zone_id].total_mass)
    )
    attraction_destination = attraction_from_total_mass(
        float(zone_mass_by_id[destination_zone_id].total_mass)
    )
    decay = float(
        connectivity_by_pair.get(
            (origin_zone_id, destination_zone_id),
            PairConnectivity(
                origin_zone_id=origin_zone_id,
                destination_zone_id=destination_zone_id,
                decay=1.0,
                direct_link_count=0,
                shared_stop_count=0,
            ),
        ).decay
    )
    noise = pair_noise(seed, origin_zone_id, destination_zone_id)
    passengers = strategy.demand_scale * attraction_origin * attraction_destination * decay * noise
    passengers = max(passengers, float(strategy.min_passengers))
    return DailyOdDemand(
        origin_zone_id=origin_zone_id,
        destination_zone_id=destination_zone_id,
        passengers=PassengerCount(passengers),
    )


def attraction_from_total_mass(total_mass: float) -> float:
    if total_mass < 0.0 or not math.isfinite(total_mass):
        raise ValueError(f"zone total mass must be finite and non-negative, got {total_mass}")
    return math.sqrt(total_mass)
