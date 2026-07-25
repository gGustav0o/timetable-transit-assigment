#include "timetable/domain/assignment/search/cost/exposure.hpp"

#include <cmath>
#include <string>

#include <mathfp/core/error.hpp>
#include <mathfp/core/summation.hpp>
#include <mathfp/core/try.hpp>
#include <mathfp/core/unit.hpp>

#include "timetable/domain/assignment/capacity_aware/penalty.hpp"
#include "timetable/domain/assignment/search/cost/capacity_index.hpp"

namespace timetable::domain::assignment {
    namespace {

        [[nodiscard]] mathfp::Expected<mathfp::Unit> ensure_finite_nonnegative(
              double      value
            , const char* field
        ) {
            if (std::isfinite(value) && value >= 0.0) {
                return mathfp::kUnit;
            }

            return mathfp::unexpected(
                mathfp::invalid_arg("search cost scalar must be finite and non-negative")
                    .ctx("field", field)
                    .ctx("value", value)
            );
        }

        [[nodiscard]] mathfp::Expected<mathfp::Unit> ensure_search_ride_leg_occupancy_identity(
            const ConnectionLeg& ride_leg
        ) {
            if (
                   ride_leg.trip.has_value()
                && ride_leg.occurrence_from.has_value()
                && ride_leg.occurrence_to.has_value()
            ) {
                return mathfp::kUnit;
            }

            return mathfp::unexpected(
                mathfp::invalid_arg("capacity-aware search ride leg is missing occupancy identity")
                    .ctx("has_trip"           , ride_leg.trip.has_value() ? "true" : "false")
                    .ctx("has_occurrence_from", ride_leg.occurrence_from.has_value() ? "true" : "false")
                    .ctx("has_occurrence_to"  , ride_leg.occurrence_to.has_value() ? "true" : "false")
            );
        }

    }  // namespace

    mathfp::Expected<CapacityExposure> search_capacity_exposure(
          const Connection&               connection
        , IntervalId                      interval
        , const SearchCapacityCostConfig& capacity
    ) {
        mathfp::CompensatedSum<double> exposure_seconds;
        for (const auto& leg : connection.trace.legs) {
            if (!is_ride_leg(leg.kind)) {
                continue;
            }

            MATHFP_TRY_LET(
                  CapacityExposure
                , exposure
                , search_capacity_exposure(leg, interval, capacity)
            );
            exposure_seconds.add(exposure.equivalent_time.value());
        }

        return make_capacity_exposure(Time{ exposure_seconds.value() });
    }

    mathfp::Expected<CapacityExposure> search_capacity_exposure(
          const ConnectionLeg&            ride_leg
        , IntervalId                      interval
        , const SearchCapacityCostConfig& capacity
    ) {
        if (!is_ride_leg(ride_leg.kind)) {
            return mathfp::unexpected(
                mathfp::invalid_arg("capacity-aware search exposure can only be computed for ride legs")
                    .ctx("leg_kind", std::string(to_string(ride_leg.kind)))
            );
        }
        MATHFP_TRY(ensure_search_ride_leg_occupancy_identity(ride_leg));

        const auto duration = ride_leg.end_time.value() - ride_leg.start_time.value();
        MATHFP_TRY(ensure_finite_nonnegative(duration, "ride_leg_duration"));

        const auto first = ride_leg.occurrence_from->position.get();
        const auto last  = ride_leg.occurrence_to->position.get();
        if (!(first < last)) {
            return mathfp::unexpected(
                mathfp::invalid_arg("capacity-aware search ride leg occupancy interval must satisfy from_index < to_index")
                    .ctx("trip_id"   , ride_leg.trip->get())
                    .ctx("from_index", first)
                    .ctx("to_index"  , last)
            );
        }

        const auto item_duration = duration
            / static_cast<double>(last - first);

        MATHFP_TRY_LET(
              double
            , penalty_sum
            , search_capacity_penalty_sum(
                  capacity
                , interval
                , *ride_leg.trip
                , RoutePosition{ first }
                , RoutePosition{ last }
            )
        );
        return make_capacity_exposure(Time{ item_duration * penalty_sum });
    }

    mathfp::Expected<CapacityExposure> search_capacity_exposure(
          const SearchConnection&         connection
        , IntervalId                      interval
        , const SearchCapacityCostConfig& capacity
    ) {
        return search_capacity_exposure(
              canonical_connection(connection)
            , interval
            , capacity
        );
    }

}  // namespace timetable::domain::assignment
