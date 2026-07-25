#include "timetable/domain/assignment/capacity/overload_assessment.hpp"

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <utility>

#include <mathfp/core/error.hpp>
#include <mathfp/core/numeric_tolerance.hpp>
#include <mathfp/core/try.hpp>

#include "timetable/domain/assignment/capacity/load_projection.hpp"
#include "timetable/domain/assignment/capacity/overload.hpp"

namespace timetable::domain::assignment {
    namespace {

        [[nodiscard]] mathfp::Expected<mathfp::Unit> ensure_nonnegative_id(
              std::int64_t raw
            , const char*  name
        ) {
            if (raw >= 0) {
                return mathfp::kUnit;
            }

            return mathfp::unexpected(
                mathfp::invalid_arg("vehicle journey item key must not contain negative ids")
                    .ctx("field", name)
                    .ctx("value", raw)
            );
        }

        [[nodiscard]] VehicleJourneyItemLoad make_vehicle_journey_item_load(
              const VehicleJourneyItemLoadKey& key
            , double                           passengers
        ) noexcept {
            return VehicleJourneyItemLoad{
                  .key        = key
                , .passengers = passengers
            };
        }

        using VehicleJourneyItemLoadLookup =
            std::map<VehicleJourneyItemLoadKey, const VehicleJourneyItemLoad*>;
        using VehicleJourneyItemCapacityMap =
            std::map<VehicleJourneyItemKey, const VehicleJourneyItemCapacity*>;
        using VehicleJourneyItemIntervalMap =
            std::map<IntervalId, const TimeInterval*>;
        using VehicleJourneyItemOverloadMap =
            std::map<VehicleJourneyItemLoadKey, const VehicleJourneyItemOverload*>;

        [[nodiscard]] VehicleJourneyItemCapacityMap build_capacity_lookup(
            const VehicleJourneyItemCapacitySet& capacities
        ) {
            VehicleJourneyItemCapacityMap lookup;
            for (const auto& capacity : capacities.items) {
                lookup.emplace(capacity.key, &capacity);
            }
            return lookup;
        }

        [[nodiscard]] VehicleJourneyItemLoadLookup build_load_lookup(
            const VehicleJourneyItemLoads& loads
        ) {
            VehicleJourneyItemLoadLookup lookup;
            for (const auto& load : loads.items) {
                lookup.emplace(load.key, &load);
            }
            return lookup;
        }

        mathfp::Expected<VehicleJourneyItemIntervalMap> build_interval_lookup_for_capacity_assessment(
            const std::vector<TimeInterval>& intervals
        ) {
            VehicleJourneyItemIntervalMap lookup;

            for (std::size_t i = 0; i < intervals.size(); ++i) {
                const auto& interval = intervals[i];
                MATHFP_TRY(ensure_nonnegative_id(interval.id.get(), "interval"));

                if (!(interval.start.value() < interval.end.value())) {
                    return mathfp::unexpected(
                        mathfp::invalid_arg("vehicle journey item overload assessment interval must satisfy start < end")
                            .ctx("interval_id", interval.id.get())
                            .ctx("start"      , interval.start.value())
                            .ctx("end"        , interval.end.value())
                    );
                }

                if (const auto [existing, inserted] = lookup.emplace(interval.id, &interval); !inserted) {
                    return mathfp::unexpected(
                        mathfp::invalid_arg("duplicate vehicle journey item overload assessment interval id")
                            .ctx("interval_id"           , interval.id.get())
                            .ctx("first_interval_start"  , existing->second->start.value())
                            .ctx("first_interval_end"    , existing->second->end.value())
                            .ctx("current_interval_index", static_cast<std::int64_t>(i))
                    );
                }
            }

            return lookup;
        }

        [[nodiscard]] double vehicle_journey_item_passengers_or_zero(
              const VehicleJourneyItemLoadLookup& loads
            , const VehicleJourneyItemLoadKey&    key
        ) noexcept {
            const auto load_it = loads.find(key);
            if (load_it == loads.end()) {
                return 0.0;
            }
            return load_it->second->passengers;
        }

        [[nodiscard]] bool almost_equal_scalar(
              double lhs
            , double rhs
        ) noexcept {
            return mathfp::almost_equal(lhs, rhs);
        }

        [[nodiscard]] bool almost_equal_optional_scalar(
              const std::optional<double>& lhs
            , const std::optional<double>& rhs
        ) noexcept {
            if (lhs.has_value() != rhs.has_value()) {
                return false;
            }
            return !lhs.has_value() || almost_equal_scalar(*lhs, *rhs);
        }

        [[nodiscard]] VehicleJourneyItemOverloadMap build_overload_lookup(
            const VehicleJourneyItemOverloadAssessment& assessment
        ) {
            VehicleJourneyItemOverloadMap lookup;
            for (const auto& overload : assessment.items) {
                lookup.emplace(overload.key, &overload);
            }
            return lookup;
        }

        mathfp::Expected<mathfp::Unit> validate_expected_overload_row(
              const VehicleJourneyItemOverload& actual
            , const VehicleJourneyItemOverload& expected
        ) {
            if (actual.key == expected.key
                && almost_equal_scalar(actual.passengers, expected.passengers)
                && almost_equal_optional_scalar(actual.total_capacity, expected.total_capacity)
                && almost_equal_optional_scalar(actual.seat_capacity, expected.seat_capacity)
                && almost_equal_optional_scalar(actual.load_factor, expected.load_factor)
                && almost_equal_optional_scalar(actual.overload_passengers, expected.overload_passengers)
                && actual.status == expected.status) {
                return mathfp::kUnit;
            }

            return mathfp::unexpected(
                mathfp::internal_error("vehicle journey item overload row disagrees with full-support assessment semantics")
                    .ctx("interval_id"        , actual.key.interval.get())
                    .ctx("trip_id"            , actual.key.item.trip.get())
                    .ctx("from_index"         , actual.key.item.from_index.get())
                    .ctx("actual_status"      , std::string(to_string(actual.status)))
                    .ctx("expected_status"    , std::string(to_string(expected.status)))
                    .ctx("actual_passengers"  , actual.passengers)
                    .ctx("expected_passengers", expected.passengers)
            );
        }

        mathfp::Expected<mathfp::Unit> validate_loaded_item_intervals(
              const VehicleJourneyItemLoads&       loads
            , const VehicleJourneyItemIntervalMap& intervals
        ) {
            for (const auto& load : loads.items) {
                if (intervals.find(load.key.interval) == intervals.end()) {
                    return mathfp::unexpected(
                        mathfp::invalid_arg("vehicle journey item load references interval outside overload assessment support")
                            .ctx("interval_id", load.key.interval.get())
                            .ctx("trip_id"    , load.key.item.trip.get())
                            .ctx("from_index" , load.key.item.from_index.get())
                    );
                }
            }

            return mathfp::kUnit;
        }

        [[nodiscard]] std::size_t missing_capacity_load_count(
              const VehicleJourneyItemLoads&       loads
            , const VehicleJourneyItemCapacityMap& capacities
        ) noexcept {
            std::size_t count = 0;
            for (const auto& load : loads.items) {
                if (capacities.find(load.key.item) == capacities.end()) {
                    ++count;
                }
            }
            return count;
        }

        mathfp::Expected<mathfp::Unit> validate_vehicle_journey_item_overload_full_support(
              const VehicleJourneyItemOverloadAssessment& assessment
            , const VehicleJourneyItemLoads&              loads
            , const VehicleJourneyItemCapacitySet&        capacities
            , const std::vector<TimeInterval>&            intervals
            , const VehicleJourneyItemLoadLookup&         load_lookup
            , const VehicleJourneyItemCapacityMap&        capacity_lookup
            , const VehicleJourneyItemIntervalMap&        interval_lookup
        ) {
            MATHFP_TRY(validate_loaded_item_intervals(loads, interval_lookup));

            const auto expected_count =
                  intervals.size() * capacities.items.size()
                + missing_capacity_load_count(loads, capacity_lookup);
            if (assessment.items.size() != expected_count) {
                return mathfp::unexpected(
                    mathfp::internal_error("vehicle journey item overload assessment row count disagrees with full reporting support")
                        .ctx("actual_row_count"  , static_cast<std::int64_t>(assessment.items.size()))
                        .ctx("expected_row_count", static_cast<std::int64_t>(expected_count))
                        .ctx("interval_count"    , static_cast<std::int64_t>(intervals.size()))
                        .ctx("capacity_count"    , static_cast<std::int64_t>(capacities.items.size()))
                );
            }

            const auto overload_lookup = build_overload_lookup(assessment);

            for (const auto& interval : intervals) {
                for (const auto& capacity : capacities.items) {
                    const auto key = VehicleJourneyItemLoadKey{
                          .interval = interval.id
                        , .item     = capacity.key
                    };
                    const auto overload_it = overload_lookup.find(key);
                    if (overload_it == overload_lookup.end()) {
                        return mathfp::unexpected(
                            mathfp::internal_error("vehicle journey item overload assessment is missing a capacity-supported row")
                                .ctx("interval_id", key.interval.get())
                                .ctx("trip_id"    , key.item.trip.get())
                                .ctx("from_index" , key.item.from_index.get())
                        );
                    }

                    const auto expected_load = make_vehicle_journey_item_load(
                          key
                        , vehicle_journey_item_passengers_or_zero(load_lookup, key)
                    );
                    MATHFP_TRY_LET(
                          VehicleJourneyItemOverload
                        , expected_overload
                        , compute_vehicle_journey_item_overload(
                              expected_load
                            , capacity
                        )
                    );
                    MATHFP_TRY(validate_expected_overload_row(
                          *overload_it->second
                        , expected_overload
                    ));
                }
            }

            for (const auto& load : loads.items) {
                if (capacity_lookup.find(load.key.item) != capacity_lookup.end()) {
                    continue;
                }

                const auto overload_it = overload_lookup.find(load.key);
                if (overload_it == overload_lookup.end()) {
                    return mathfp::unexpected(
                        mathfp::internal_error("vehicle journey item overload assessment is missing a missing-capacity loaded row")
                            .ctx("interval_id", load.key.interval.get())
                            .ctx("trip_id"    , load.key.item.trip.get())
                            .ctx("from_index" , load.key.item.from_index.get())
                    );
                }

                MATHFP_TRY_LET(
                      VehicleJourneyItemOverload
                    , expected_overload
                    , make_missing_capacity_vehicle_journey_item_overload(load)
                );
                MATHFP_TRY(validate_expected_overload_row(
                      *overload_it->second
                    , expected_overload
                ));
            }

            return mathfp::kUnit;
        }

    }  // namespace

    VehicleJourneyItemOverloadAssessment make_skipped_assignment_disabled_vehicle_journey_item_overload_assessment() {
        return VehicleJourneyItemOverloadAssessment{
              .status = VehicleJourneyItemOverloadAssessmentStatus::SkippedAssignmentDisabled
            , .items  = {}
        };
    }

    VehicleJourneyItemOverloadAssessment make_disabled_by_config_vehicle_journey_item_overload_assessment() {
        return VehicleJourneyItemOverloadAssessment{
              .status = VehicleJourneyItemOverloadAssessmentStatus::DisabledByConfig
            , .items  = {}
        };
    }

    VehicleJourneyItemOverloadAssessment make_missing_capacity_input_vehicle_journey_item_overload_assessment() {
        return VehicleJourneyItemOverloadAssessment{
              .status = VehicleJourneyItemOverloadAssessmentStatus::MissingCapacityInput
            , .items  = {}
        };
    }

    mathfp::Expected<VehicleJourneyItemOverloadAssessment> assess_vehicle_journey_item_overload(
          const VehicleJourneyItemLoads&       loads
        , const VehicleJourneyItemCapacitySet& capacities
        , const std::vector<TimeInterval>&     intervals
    ) {
        MATHFP_TRY(validate_vehicle_journey_item_loads(loads));
        MATHFP_TRY(validate_vehicle_journey_item_capacity_set(capacities));
        MATHFP_TRY_LET(
              VehicleJourneyItemIntervalMap
            , interval_lookup
            , build_interval_lookup_for_capacity_assessment(intervals)
        );
        const auto capacity_lookup = build_capacity_lookup(capacities);
        const auto load_lookup     = build_load_lookup(loads);

        MATHFP_TRY(validate_loaded_item_intervals(loads, interval_lookup));

        VehicleJourneyItemOverloadAssessment assessment;
        assessment.status = VehicleJourneyItemOverloadAssessmentStatus::Calculated;
        assessment.items.reserve(
              intervals.size() * capacities.items.size()
            + loads.items.size()
        );

        for (const auto& interval : intervals) {
            for (const auto& capacity : capacities.items) {
                const auto key = VehicleJourneyItemLoadKey{
                      .interval = interval.id
                    , .item     = capacity.key
                };
                MATHFP_TRY_LET(
                      VehicleJourneyItemOverload
                    , overload
                    , compute_vehicle_journey_item_overload(
                          make_vehicle_journey_item_load(
                              key
                            , vehicle_journey_item_passengers_or_zero(load_lookup, key)
                          )
                        , capacity
                    )
                );
                assessment.items.push_back(std::move(overload));
            }
        }

        for (const auto& load : loads.items) {
            const auto capacity_it = capacity_lookup.find(load.key.item);
            if (capacity_it == capacity_lookup.end()) {
                MATHFP_TRY_LET(
                      VehicleJourneyItemOverload
                    , overload
                    , make_missing_capacity_vehicle_journey_item_overload(load)
                );
                assessment.items.push_back(std::move(overload));
            }
        }

        MATHFP_TRY(validate_vehicle_journey_item_overload_assessment(assessment));
        MATHFP_TRY(validate_vehicle_journey_item_overload_full_support(
              assessment
            , loads
            , capacities
            , intervals
            , load_lookup
            , capacity_lookup
            , interval_lookup
        ));
        return assessment;
    }

    mathfp::Expected<ElementarySegmentOverloadAssessment> assess_elementary_segment_overload(
          const ElementarySegmentLoads&        loads
        , const VehicleJourneyItemCapacitySet& capacities
        , const std::vector<TimeInterval>&     intervals
    ) {
        return assess_vehicle_journey_item_overload(
              loads
            , capacities
            , intervals
        );
    }

}  // namespace timetable::domain::assignment
