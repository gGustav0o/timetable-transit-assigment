"""Demand synthesis model.

This layer starts only after structural extraction is complete and validated.
It owns:

- modeling intervals
- synthetic daily OD demand
- temporal demand shares
- final DemandEntry-compatible rows

The layer consumes `StructuralModel` and produces synthetic demand artifacts.
"""

from demand_generator.types import (
    DailyOdDemand,
    DemandEntryRow,
    SynthesisModel,
    TemporalShare,
    TimeIntervalRow,
)

__all__ = [
    "DailyOdDemand",
    "DemandEntryRow",
    "SynthesisModel",
    "TemporalShare",
    "TimeIntervalRow",
]
