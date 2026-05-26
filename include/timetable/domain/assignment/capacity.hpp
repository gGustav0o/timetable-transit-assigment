#pragma once

#include <array>
#include <compare>
#include <cstdint>
#include <map>
#include <optional>
#include <string_view>
#include <vector>

#include <mathfp/core/expected.hpp>
#include <mathfp/core/summation.hpp>
#include <mathfp/core/unit.hpp>

#include "timetable/enum_string.hpp"
#include "timetable/domain/assignment/connection.hpp"
#include "timetable/domain/model.hpp"

namespace timetable::domain::assignment {

    struct DemandSplitResult;

    /**
     * @brief One physical vehicle journey item between adjacent stop positions.
     *
     * The key corresponds to one elementary in-vehicle occupancy interval:
     * passengers boarding at from_index and alighting later occupy this item
     * when from_index belongs to the half-open ride interval [from, to).
     */
    struct VehicleJourneyItemKey final {
        TripId        trip{};
        RoutePosition from_index{};

        auto operator<=>(const VehicleJourneyItemKey&) const = default;
    };

    /**
     * @brief Capacity data for one vehicle journey item.
     *
     * total_capacity is the hard passenger capacity used for overload
     * assessment. seat_capacity is retained as an analytical attribute and must
     * not be treated as overload capacity unless a separate policy says so.
     */
    struct VehicleJourneyItemCapacity final {
        VehicleJourneyItemKey key{};
        double                total_capacity{};
        double                seat_capacity{};
    };

    /**
     * @brief Domain collection of vehicle journey item capacities.
     *
     * The vector preserves input/order semantics for deterministic diagnostics
     * and export. Validation guarantees unique keys.
     */
    struct VehicleJourneyItemCapacitySet final {
        std::vector<VehicleJourneyItemCapacity> items{};
    };

    enum class VehicleJourneyItemCapacityInputStatus : std::uint8_t {
          MissingInput
        , Loaded
    };

    inline constexpr std::array kVehicleJourneyItemCapacityInputStatusTokens{
          timetable::EnumStringEntry<VehicleJourneyItemCapacityInputStatus>{
              VehicleJourneyItemCapacityInputStatus::MissingInput, "missing_input"
          }
        , timetable::EnumStringEntry<VehicleJourneyItemCapacityInputStatus>{
              VehicleJourneyItemCapacityInputStatus::Loaded, "loaded"
          }
    };

    [[nodiscard]] inline constexpr std::string_view to_string(
        VehicleJourneyItemCapacityInputStatus value
    ) noexcept {
        return timetable::enum_to_string(value, kVehicleJourneyItemCapacityInputStatusTokens);
    }

    /**
     * @brief Optional capacity input attached to an assignment run.
     *
     * MissingInput means veh_journey_item_cap.csv was not provided. It is not a
     * hard input error, but downstream overload assessment must report a
     * disabled/missing-input status instead of interpreting the empty set as
     * zero available capacity.
     */
    struct VehicleJourneyItemCapacityInput final {
        VehicleJourneyItemCapacityInputStatus status{ VehicleJourneyItemCapacityInputStatus::MissingInput };
        VehicleJourneyItemCapacitySet         capacities{};
    };

    /**
     * @brief Interval-specific key for demand-induced vehicle journey item load.
     */
    struct VehicleJourneyItemLoadKey final {
        IntervalId            interval{};
        VehicleJourneyItemKey item{};

        auto operator<=>(const VehicleJourneyItemLoadKey&) const = default;
    };

    /**
     * @brief Passenger load projected to one vehicle journey item.
     */
    struct VehicleJourneyItemLoad final {
        VehicleJourneyItemLoadKey key{};
        double                    passengers{};
    };

    /**
     * @brief Demand-induced vehicle journey item load profile.
     *
     * This is intentionally separate from AssignmentLoads. AssignmentLoads
     * reports selected ride-segment flows, while this profile expands each ride
     * leg to adjacent in-vehicle occupancy items.
     *
     * The profile is sparse by design: it contains only vehicle journey items
     * with positive demand-induced load. Zero-load rows belong to the
     * capacity-anchored overload assessment, not to this load projection.
     */
    struct VehicleJourneyItemLoads final {
        std::vector<VehicleJourneyItemLoad> items{};
    };

    /**
     * @brief Primary elementary route-segment load representation.
     *
     * A vehicle journey item is the time-realized elementary segment of a
     * public-transport route: one trip on the half-open stop-position interval
     * [from_index, from_index + 1). These aliases name that mathematical role
     * directly while preserving the existing capacity API.
     */
    using ElementarySegmentKey     = VehicleJourneyItemKey;
    using ElementarySegmentLoadKey = VehicleJourneyItemLoadKey;
    using ElementarySegmentLoad    = VehicleJourneyItemLoad;
    using ElementarySegmentLoads   = VehicleJourneyItemLoads;

    struct ElementarySegmentLoadAccumulator final {
        std::map<ElementarySegmentLoadKey, mathfp::CompensatedSum<double>> loads{};
    };

    enum class VehicleJourneyItemOverloadStatus : std::uint8_t {
          Ok
        , Overloaded
        , MissingCapacity
        , ZeroCapacity
    };

    inline constexpr std::array kVehicleJourneyItemOverloadStatusTokens{
          timetable::EnumStringEntry<VehicleJourneyItemOverloadStatus>{
              VehicleJourneyItemOverloadStatus::Ok, "ok"
          }
        , timetable::EnumStringEntry<VehicleJourneyItemOverloadStatus>{
              VehicleJourneyItemOverloadStatus::Overloaded, "overloaded"
          }
        , timetable::EnumStringEntry<VehicleJourneyItemOverloadStatus>{
              VehicleJourneyItemOverloadStatus::MissingCapacity, "missing_capacity"
          }
        , timetable::EnumStringEntry<VehicleJourneyItemOverloadStatus>{
              VehicleJourneyItemOverloadStatus::ZeroCapacity, "zero_capacity"
          }
    };

    [[nodiscard]] inline constexpr std::string_view to_string(
        VehicleJourneyItemOverloadStatus value
    ) noexcept {
        return timetable::enum_to_string(value, kVehicleJourneyItemOverloadStatusTokens);
    }

    /**
     * @brief Capacity assessment for one loaded vehicle journey item.
     *
     * total_capacity, seat_capacity, load_factor and overload_passengers are
     * absent when the capacity row is missing. For zero total capacity,
     * load_factor is absent because division by zero is not a scalar load
     * factor, while overload_passengers remains well-defined as passengers.
     */
    struct VehicleJourneyItemOverload final {
        VehicleJourneyItemLoadKey                 key{};
        double                                    passengers{};
        std::optional<double>                     total_capacity{};
        std::optional<double>                     seat_capacity{};
        std::optional<double>                     load_factor{};
        std::optional<double>                     overload_passengers{};
        VehicleJourneyItemOverloadStatus          status{ VehicleJourneyItemOverloadStatus::MissingCapacity };
    };

    enum class VehicleJourneyItemOverloadAssessmentStatus : std::uint8_t {
          SkippedAssignmentDisabled
        , DisabledByConfig
        , MissingCapacityInput
        , Calculated
    };

    inline constexpr std::array kVehicleJourneyItemOverloadAssessmentStatusTokens{
          timetable::EnumStringEntry<VehicleJourneyItemOverloadAssessmentStatus>{
              VehicleJourneyItemOverloadAssessmentStatus::SkippedAssignmentDisabled, "skipped_assignment_disabled"
          }
        , timetable::EnumStringEntry<VehicleJourneyItemOverloadAssessmentStatus>{
              VehicleJourneyItemOverloadAssessmentStatus::DisabledByConfig, "disabled_by_config"
          }
        , timetable::EnumStringEntry<VehicleJourneyItemOverloadAssessmentStatus>{
              VehicleJourneyItemOverloadAssessmentStatus::MissingCapacityInput, "missing_capacity_input"
          }
        , timetable::EnumStringEntry<VehicleJourneyItemOverloadAssessmentStatus>{
              VehicleJourneyItemOverloadAssessmentStatus::Calculated, "calculated"
          }
    };

    [[nodiscard]] inline constexpr std::string_view to_string(
        VehicleJourneyItemOverloadAssessmentStatus value
    ) noexcept {
        return timetable::enum_to_string(value, kVehicleJourneyItemOverloadAssessmentStatusTokens);
    }

    /**
     * @brief Capacity-anchored overload assessment for vehicle journey items.
     *
     * When capacity input is loaded, a calculated assessment is defined on the
     * full reporting support:
     *
     *   (time intervals x capacity rows) union loaded items without capacity.
     *
     * Therefore capacity rows with zero passengers are materialized as explicit
     * rows. Loaded items without a capacity row remain visible with
     * MissingCapacity status.
     */
    struct VehicleJourneyItemOverloadAssessment final {
        VehicleJourneyItemOverloadAssessmentStatus status{
            VehicleJourneyItemOverloadAssessmentStatus::MissingCapacityInput
        };
        std::vector<VehicleJourneyItemOverload> items{};
    };

    using ElementarySegmentOverload = VehicleJourneyItemOverload;
    using ElementarySegmentOverloadAssessment = VehicleJourneyItemOverloadAssessment;

    [[nodiscard]] mathfp::Expected<mathfp::Unit> validate_vehicle_journey_item_key(
        VehicleJourneyItemKey key
    );

    [[nodiscard]] mathfp::Expected<mathfp::Unit> validate_vehicle_journey_item_capacity(
        const VehicleJourneyItemCapacity& capacity
    );

    [[nodiscard]] mathfp::Expected<mathfp::Unit> validate_vehicle_journey_item_capacity_set(
        const VehicleJourneyItemCapacitySet& capacities
    );

    [[nodiscard]] mathfp::Expected<VehicleJourneyItemCapacity> make_vehicle_journey_item_capacity(
          VehicleJourneyItemKey key
        , double                total_capacity
        , double                seat_capacity
    );

    [[nodiscard]] mathfp::Expected<VehicleJourneyItemCapacitySet> make_vehicle_journey_item_capacity_set(
        std::vector<VehicleJourneyItemCapacity> items
    );

    [[nodiscard]] mathfp::Expected<mathfp::Unit> validate_vehicle_journey_item_capacity_input(
        const VehicleJourneyItemCapacityInput& input
    );

    [[nodiscard]] VehicleJourneyItemCapacityInput make_missing_vehicle_journey_item_capacity_input();

    [[nodiscard]] mathfp::Expected<VehicleJourneyItemCapacityInput> make_loaded_vehicle_journey_item_capacity_input(
        VehicleJourneyItemCapacitySet capacities
    );

    /**
     * @brief Expand a ride interval to occupied vehicle journey item keys.
     *
     * The interval is strictly half-open: [from_index, to_index). A passenger
     * riding from position i to position j occupies items i, i + 1, ..., j - 1.
     */
    [[nodiscard]] mathfp::Expected<std::vector<VehicleJourneyItemKey>> vehicle_journey_items_occupied(
          TripId        trip
        , RoutePosition from_index
        , RoutePosition to_index
    );

    /**
     * @brief Project one validated ride leg to occupied vehicle journey items.
     *
     * Only ride legs with trip, occurrence_from and occurrence_to can be
     * projected. The occurrence positions define the half-open occupancy
     * interval.
     */
    [[nodiscard]] mathfp::Expected<std::vector<VehicleJourneyItemKey>> vehicle_journey_items_occupied(
        const ConnectionLeg& leg
    );

    [[nodiscard]] mathfp::Expected<mathfp::Unit> validate_vehicle_journey_item_load(
        const VehicleJourneyItemLoad& load
    );

    [[nodiscard]] mathfp::Expected<mathfp::Unit> validate_vehicle_journey_item_loads(
        const VehicleJourneyItemLoads& loads
    );

    [[nodiscard]] mathfp::Expected<mathfp::Unit> validate_vehicle_journey_item_load_projection(
          const DemandSplitResult&       split_result
        , const VehicleJourneyItemLoads& loads
    );

    [[nodiscard]] mathfp::Expected<VehicleJourneyItemLoads> build_vehicle_journey_item_loads(
        const DemandSplitResult& split_result
    );

    [[nodiscard]] mathfp::Expected<ElementarySegmentLoads> build_elementary_segment_loads(
        const DemandSplitResult& split_result
    );

    [[nodiscard]] mathfp::Expected<ElementarySegmentLoads> build_day_path_elementary_segment_loads(
        const DemandSplitResult& split_result
    );

    [[nodiscard]] mathfp::Expected<mathfp::Unit> accumulate_elementary_segment_loads(
          ElementarySegmentLoadAccumulator& accumulator
        , const ElementarySegmentLoads&     loads
    );

    [[nodiscard]] mathfp::Expected<ElementarySegmentLoads> materialize_elementary_segment_loads(
        const ElementarySegmentLoadAccumulator& accumulator
    );

    [[nodiscard]] mathfp::Expected<mathfp::Unit> validate_vehicle_journey_item_overload(
        const VehicleJourneyItemOverload& overload
    );

    [[nodiscard]] mathfp::Expected<mathfp::Unit> validate_vehicle_journey_item_overload_assessment(
        const VehicleJourneyItemOverloadAssessment& assessment
    );

    [[nodiscard]] VehicleJourneyItemOverloadAssessment make_skipped_assignment_disabled_vehicle_journey_item_overload_assessment();

    [[nodiscard]] VehicleJourneyItemOverloadAssessment make_disabled_by_config_vehicle_journey_item_overload_assessment();

    [[nodiscard]] VehicleJourneyItemOverloadAssessment make_missing_capacity_input_vehicle_journey_item_overload_assessment();

    [[nodiscard]] mathfp::Expected<VehicleJourneyItemOverloadAssessment> assess_vehicle_journey_item_overload(
          const VehicleJourneyItemLoads&       loads
        , const VehicleJourneyItemCapacitySet& capacities
        , const std::vector<TimeInterval>&     intervals
    );

    [[nodiscard]] mathfp::Expected<ElementarySegmentOverloadAssessment> assess_elementary_segment_overload(
          const ElementarySegmentLoads&        loads
        , const VehicleJourneyItemCapacitySet& capacities
        , const std::vector<TimeInterval>&     intervals
    );

}  // namespace timetable::domain::assignment
