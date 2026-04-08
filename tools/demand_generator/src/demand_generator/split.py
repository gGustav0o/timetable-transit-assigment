from __future__ import annotations

from demand_generator.temporal_profile import build_temporal_profile_for_od
from demand_generator.types import (
    DailyOdDemand,
    DemandEntryRow,
    PassengerCount,
    TemporalProfileStrategy,
    TemporalShare,
    TimeIntervalRow,
)
from demand_generator.validation import validate_demand_entries, validate_temporal_shares


def build_temporal_shares_for_daily_demand(
    daily_demand: tuple[DailyOdDemand, ...],
    intervals: tuple[TimeIntervalRow, ...],
    seed: int,
    strategy: TemporalProfileStrategy,
) -> tuple[TemporalShare, ...]:
    return validate_temporal_shares(tuple(
        share
        for od in daily_demand
        for share in build_temporal_profile_for_od(
            seed=seed,
            origin_zone_id=od.origin_zone_id,
            destination_zone_id=od.destination_zone_id,
            intervals=intervals,
            strategy=strategy,
        )
    ))


def split_daily_demand(
    daily_demand: tuple[DailyOdDemand, ...],
    temporal_shares: tuple[TemporalShare, ...],
) -> tuple[DemandEntryRow, ...]:
    daily_by_pair = {
        (entry.origin_zone_id, entry.destination_zone_id): entry
        for entry in daily_demand
    }
    return validate_demand_entries(tuple(
        split_share_to_demand_entry(
            daily=daily_by_pair[(share.origin_zone_id, share.destination_zone_id)],
            share=share,
        )
        for share in temporal_shares
    ), daily_demand=daily_demand)


def split_share_to_demand_entry(
    daily: DailyOdDemand,
    share: TemporalShare,
) -> DemandEntryRow:
    passengers = float(daily.passengers) * float(share.share)
    return DemandEntryRow(
        origin_zone_id=share.origin_zone_id,
        destination_zone_id=share.destination_zone_id,
        interval_id=share.interval_id,
        passengers=PassengerCount(passengers),
    )
