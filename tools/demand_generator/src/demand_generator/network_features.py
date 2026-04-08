from __future__ import annotations

import math
from collections import Counter, defaultdict

from demand_generator.types import (
    ConnectivityDecayStrategy,
    Mass,
    PairConnectivity,
    ParsedSupply,
    Seconds,
    StopId,
    StopStopLink,
    StructuralModel,
    TimeRange,
    Weight,
    ZoneId,
    ZoneMassStrategy,
    ZoneMassComponents,
    ZoneStopLink,
    ZoneStructure,
)
from demand_generator.validation import validate_structural_model


DEFAULT_ZONE_MASS_STRATEGY = ZoneMassStrategy(
    base_mass=Mass(1.0),
    incident_access_weight=1.0,
    unique_stop_weight=2.0,
    endpoint_segment_weight=0.5,
)

DEFAULT_CONNECTIVITY_DECAY_STRATEGY = ConnectivityDecayStrategy(
    min_decay=Weight(0.25),
    direct_link_weight=1.0,
    shared_stop_weight=0.5,
)


def build_structural_model(
    supply: ParsedSupply,
    zone_mass_strategy: ZoneMassStrategy = DEFAULT_ZONE_MASS_STRATEGY,
    decay_strategy: ConnectivityDecayStrategy = DEFAULT_CONNECTIVITY_DECAY_STRATEGY,
) -> StructuralModel:
    zone_ids = extract_zone_ids(supply)
    inferred_time_range = infer_time_range(supply)
    zone_stop_links = build_zone_stop_links(supply)
    stop_stop_links = build_stop_stop_links(supply)
    zones = build_zone_structures(supply, zone_ids, zone_stop_links, zone_mass_strategy)
    pair_connectivity = build_pair_connectivity(supply, zones, decay_strategy)
    return validate_structural_model(StructuralModel(
        supply=supply,
        zone_ids=zone_ids,
        inferred_time_range=inferred_time_range,
        zone_stop_links=zone_stop_links,
        stop_stop_links=stop_stop_links,
        zones=zones,
        pair_connectivity=pair_connectivity,
    ))


def extract_zone_ids(supply: ParsedSupply) -> tuple[ZoneId, ...]:
    zone_ids = {
        zone_id
        for row in supply.rows
        for zone_id in (row.from_zone_id, row.to_zone_id)
        if zone_id is not None
    }
    return tuple(sorted(zone_ids))


def infer_time_range(supply: ParsedSupply) -> TimeRange | None:
    observed_times = tuple(
        float(value)
        for row in supply.rows
        for value in (row.dep_sec, row.arr_sec)
        if value is not None and math.isfinite(value)
    )
    if not observed_times:
        return None

    start_sec = Seconds(int(math.floor(min(observed_times))))
    end_sec = Seconds(int(math.ceil(max(observed_times))))
    return TimeRange(start_sec=start_sec, end_sec=end_sec)


def build_zone_stop_links(supply: ParsedSupply) -> tuple[ZoneStopLink, ...]:
    unique_links = {
        link
        for row in supply.rows
        for link in zone_stop_links_for_row(row)
    }
    return tuple(sorted(unique_links, key=zone_stop_link_sort_key))


def zone_stop_links_for_row(row: object) -> tuple[ZoneStopLink, ...]:
    links: list[ZoneStopLink] = []

    from_zone_id = getattr(row, "from_zone_id")
    to_stop_id = getattr(row, "to_stop_id")
    if from_zone_id is not None and to_stop_id is not None:
        links.append(ZoneStopLink(zone_id=from_zone_id, stop_id=to_stop_id))

    to_zone_id = getattr(row, "to_zone_id")
    from_stop_id = getattr(row, "from_stop_id")
    if to_zone_id is not None and from_stop_id is not None:
        links.append(ZoneStopLink(zone_id=to_zone_id, stop_id=from_stop_id))

    return tuple(links)


def build_stop_stop_links(supply: ParsedSupply) -> tuple[StopStopLink, ...]:
    unique_links = {
        StopStopLink(from_stop_id=row.from_stop_id, to_stop_id=row.to_stop_id)
        for row in supply.rows
        if row.from_stop_id is not None and row.to_stop_id is not None
    }
    return tuple(sorted(unique_links, key=stop_stop_link_sort_key))


def build_zone_structures(
    supply: ParsedSupply,
    zone_ids: tuple[ZoneId, ...],
    zone_stop_links: tuple[ZoneStopLink, ...],
    zone_mass_strategy: ZoneMassStrategy,
) -> tuple[ZoneStructure, ...]:
    connected_stops_by_zone = collect_connected_stops_by_zone(zone_stop_links)
    incident_access_counts = count_zone_stop_incidents(supply)
    endpoint_counts = count_zone_endpoint_segments(supply)

    return tuple(
        build_zone_structure(
            zone_id=zone_id,
            connected_stop_ids=connected_stops_by_zone.get(zone_id, frozenset()),
            incident_access_count=incident_access_counts.get(zone_id, 0),
            endpoint_segment_count=endpoint_counts.get(zone_id, 0),
            strategy=zone_mass_strategy,
        )
        for zone_id in zone_ids
    )


def collect_connected_stops_by_zone(
    zone_stop_links: tuple[ZoneStopLink, ...]
) -> dict[ZoneId, frozenset[StopId]]:
    grouped: dict[ZoneId, set[StopId]] = defaultdict(set)
    for link in zone_stop_links:
        grouped[link.zone_id].add(link.stop_id)
    return {
        zone_id: frozenset(sorted(stop_ids))
        for zone_id, stop_ids in grouped.items()
    }


def count_zone_endpoint_segments(supply: ParsedSupply) -> Counter[ZoneId]:
    counts: Counter[ZoneId] = Counter()
    for row in supply.rows:
        if row.from_zone_id is not None:
            counts[row.from_zone_id] += 1
        if row.to_zone_id is not None:
            counts[row.to_zone_id] += 1
    return counts


def count_zone_stop_incidents(supply: ParsedSupply) -> Counter[ZoneId]:
    counts: Counter[ZoneId] = Counter()
    for row in supply.rows:
        if row.from_zone_id is not None and row.to_stop_id is not None:
            counts[row.from_zone_id] += 1
        if row.to_zone_id is not None and row.from_stop_id is not None:
            counts[row.to_zone_id] += 1
    return counts


def build_zone_structure(
    zone_id: ZoneId,
    connected_stop_ids: frozenset[StopId],
    incident_access_count: int,
    endpoint_segment_count: int,
    strategy: ZoneMassStrategy,
) -> ZoneStructure:
    unique_stop_count = len(connected_stop_ids)
    mass = apply_zone_mass_strategy(
        incident_access_count=incident_access_count,
        unique_stop_count=unique_stop_count,
        endpoint_segment_count=endpoint_segment_count,
        strategy=strategy,
    )
    return ZoneStructure(
        zone_id=zone_id,
        connected_stop_ids=tuple(sorted(connected_stop_ids)),
        mass=mass,
    )


def apply_zone_mass_strategy(
    incident_access_count: int,
    unique_stop_count: int,
    endpoint_segment_count: int,
    strategy: ZoneMassStrategy,
) -> ZoneMassComponents:
    derived_mass_value = (
        strategy.incident_access_weight * incident_access_count
        + strategy.unique_stop_weight * unique_stop_count
        + strategy.endpoint_segment_weight * endpoint_segment_count
    )
    base_mass = float(strategy.base_mass)
    total_mass_value = base_mass + derived_mass_value
    return ZoneMassComponents(
        base_mass=strategy.base_mass,
        derived_mass=Mass(float(derived_mass_value)),
        total_mass=Mass(float(total_mass_value)),
        incident_access_count=incident_access_count,
        unique_stop_count=unique_stop_count,
        endpoint_segment_count=endpoint_segment_count,
    )


def build_pair_connectivity(
    supply: ParsedSupply,
    zones: tuple[ZoneStructure, ...],
    decay_strategy: ConnectivityDecayStrategy,
) -> tuple[PairConnectivity, ...]:
    direct_counts = count_direct_zone_pair_links(supply)
    stop_sets = {zone.zone_id: frozenset(zone.connected_stop_ids) for zone in zones}
    pair_keys = tuple(
        (origin_zone.zone_id, destination_zone.zone_id)
        for origin_zone in zones
        for destination_zone in zones
        if origin_zone.zone_id != destination_zone.zone_id
    )

    return tuple(
        build_pair_connectivity_entry(
            origin_zone_id=origin_zone_id,
            destination_zone_id=destination_zone_id,
            direct_link_count=direct_counts.get((origin_zone_id, destination_zone_id), 0),
            shared_stop_count=len(stop_sets[origin_zone_id] & stop_sets[destination_zone_id]),
            strategy=decay_strategy,
        )
        for origin_zone_id, destination_zone_id in pair_keys
    )


def build_pair_connectivity_entry(
    origin_zone_id: ZoneId,
    destination_zone_id: ZoneId,
    direct_link_count: int,
    shared_stop_count: int,
    strategy: ConnectivityDecayStrategy,
) -> PairConnectivity:
    return PairConnectivity(
        origin_zone_id=origin_zone_id,
        destination_zone_id=destination_zone_id,
        decay=apply_decay_strategy(
            direct_link_count=direct_link_count,
            shared_stop_count=shared_stop_count,
            strategy=strategy,
        ),
        direct_link_count=direct_link_count,
        shared_stop_count=shared_stop_count,
    )


def count_direct_zone_pair_links(supply: ParsedSupply) -> Counter[tuple[ZoneId, ZoneId]]:
    counts: Counter[tuple[ZoneId, ZoneId]] = Counter()
    for row in supply.rows:
        if row.from_zone_id is None or row.to_zone_id is None:
            continue
        if row.from_zone_id == row.to_zone_id:
            continue
        counts[(row.from_zone_id, row.to_zone_id)] += 1
    return counts


def apply_decay_strategy(
    direct_link_count: int,
    shared_stop_count: int,
    strategy: ConnectivityDecayStrategy,
) -> Weight:
    signal = (
        strategy.direct_link_weight * float(direct_link_count)
        + strategy.shared_stop_weight * float(shared_stop_count)
    )
    min_decay = float(strategy.min_decay)
    value = min_decay + (1.0 - min_decay) * (signal / (signal + 1.0))
    return Weight(value)


def zone_stop_link_sort_key(link: ZoneStopLink) -> tuple[int, int]:
    return int(link.zone_id), int(link.stop_id)


def stop_stop_link_sort_key(link: StopStopLink) -> tuple[int, int]:
    return int(link.from_stop_id), int(link.to_stop_id)
