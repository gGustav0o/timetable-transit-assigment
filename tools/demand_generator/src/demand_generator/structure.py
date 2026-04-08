"""Structural extraction model.

This layer owns all data derived from the transport supply file:

- parsed supply rows
- discovered zones
- inferred timetable time range
- zone-stop / stop-stop structural relations
- regularized zone mass
- coarse inter-zone connectivity

Nothing in this layer represents passenger demand.
"""

from demand_generator.types import (
    PairConnectivity,
    ParsedSupply,
    StopStopLink,
    StructuralModel,
    SupplyRow,
    TimeRange,
    ZoneMassComponents,
    ZoneStopLink,
    ZoneStructure,
)

__all__ = [
    "PairConnectivity",
    "ParsedSupply",
    "StopStopLink",
    "StructuralModel",
    "SupplyRow",
    "TimeRange",
    "ZoneMassComponents",
    "ZoneStopLink",
    "ZoneStructure",
]
