#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

#include <mathfp/core/expected.hpp>
#include <mathfp/core/unit.hpp>

#include "timetable/enum_string.hpp"
#include "timetable/domain/assignment/assignment_period.hpp"
#include "timetable/domain/assignment/connection.hpp"
#include "timetable/domain/model.hpp"
#include "timetable/domain/scalars.hpp"

namespace timetable::domain::assignment {

    enum class DemandSegmentBasis : std::uint8_t {
          Departure
        , Arrival
    };

    inline constexpr std::array kDemandSegmentBasisTokens{
          timetable::EnumStringEntry<DemandSegmentBasis>{
              DemandSegmentBasis::Departure, "departure"
          }
        , timetable::EnumStringEntry<DemandSegmentBasis>{
              DemandSegmentBasis::Arrival, "arrival"
          }
    };

    [[nodiscard]] inline constexpr std::string_view to_string(
        DemandSegmentBasis value
    ) noexcept {
        return timetable::enum_to_string(value, kDemandSegmentBasisTokens);
    }

    [[nodiscard]] inline constexpr std::optional<DemandSegmentBasis> demand_segment_basis_from_string(
        std::string_view token
    ) noexcept {
        return timetable::enum_from_string(token, kDemandSegmentBasisTokens);
    }

    /**
     * @brief Demand-segment time semantics controlled by splitPara.
     *
     * The basis defines which realized connection time is compared with the
     * demand interval in admissibility and split temporal utility:
     * - Departure uses departure_time;
     * - Arrival uses arrival_time.
     *
     * demand_segment_delta_t follows the side of the interval that is outside
     * the preferred demand segment:
     * - Departure: departure_time - interval.end;
     * - Arrival: interval.start - arrival_time.
     */
    struct DemandSegmentTimeConfig final {
        DemandSegmentBasis basis{ DemandSegmentBasis::Departure };
        bool               consider_connections_with_positive_delta_t{ true };
    };

    /**
     * @brief Connection deletion switches controlled by choicePara.
     *
     * The switches express admissibility of already found alternatives for one
     * OD-interval task. They are separate from SearchTimePaddingPolicy because
     * they decide whether a realized connection may participate in choice/split,
     * not how demand-induced search-time catalogs are derived.
     */
    struct ConnectionDeletionConfig final {
        bool delete_outside_assignment_period{};
        bool delete_departures_before_assignment_period_for_departure_based{};
        bool delete_arrivals_after_assignment_period_for_arrival_based{};
    };

    struct ConnectionAdmissibilityConfig final {
        ConnectionDeletionConfig deletion{};
        DemandSegmentTimeConfig  demand_time{};
    };

    struct AssignmentTimeWindow final {
        Time begin{};
        Time end{};
    };

    struct ConnectionAssignmentPeriodRelation final {
        bool departure_before{};
        bool departure_after{};
        bool arrival_before{};
        bool arrival_after{};
    };

    mathfp::Expected<mathfp::Unit> validate_demand_segment_time_config(
        const DemandSegmentTimeConfig& config
    );

    mathfp::Expected<mathfp::Unit> validate_connection_deletion_config(
        const ConnectionDeletionConfig& config
    );

    mathfp::Expected<mathfp::Unit> validate_connection_admissibility_config(
        const ConnectionAdmissibilityConfig& config
    );

    [[nodiscard]] AssignmentTimeWindow assignment_period_window(
          const TimeInterval&             interval
        , const AssignmentPeriodConfig&   assignment_period
    ) noexcept;

    [[nodiscard]] bool contains(
          const AssignmentTimeWindow& window
        , Time                        value
    ) noexcept;

    [[nodiscard]] Time demand_segment_reference_time(
          const ConnectionMetrics& metrics
        , DemandSegmentBasis       basis
    ) noexcept;

    [[nodiscard]] double demand_segment_delta_t(
          const ConnectionMetrics& metrics
        , const TimeInterval&      interval
        , DemandSegmentBasis       basis
    ) noexcept;

    [[nodiscard]] ConnectionAssignmentPeriodRelation classify_assignment_period_relation(
          const ConnectionMetrics& metrics
        , const AssignmentTimeWindow& window
    ) noexcept;

    [[nodiscard]] bool connection_deleted_by_assignment_period(
          const ConnectionMetrics&              metrics
        , const TimeInterval&                   interval
        , const AssignmentPeriodConfig&         assignment_period
        , const ConnectionAdmissibilityConfig&  config
    ) noexcept;

    /**
     * @brief Hard temporal support of one demand interval.
     *
     * This is the mathematical support over which an OD-interval demand row may
     * choose connections. It is intentionally independent of the search-time
     * domain used to enumerate a tree: a service-day tree may produce many
     * connections, but only connections whose demand reference time lies inside
     * [interval.start - preAssignPeriod, interval.end + postAssignPeriod] are
     * assignable to this demand row.
     */
    [[nodiscard]] bool connection_within_demand_segment_support(
          const ConnectionMetrics&              metrics
        , const TimeInterval&                   interval
        , const AssignmentPeriodConfig&         assignment_period
        , const DemandSegmentTimeConfig&        demand_time
    ) noexcept;

    [[nodiscard]] bool connection_admissible_for_assignment_period(
          const ConnectionMetrics&              metrics
        , const TimeInterval&                   interval
        , const AssignmentPeriodConfig&         assignment_period
        , const ConnectionAdmissibilityConfig&  config
    ) noexcept;

    [[nodiscard]] bool connection_admissible_for_demand_segment(
          const ConnectionMetrics&              metrics
        , const TimeInterval&                   interval
        , const AssignmentPeriodConfig&         assignment_period
        , const ConnectionAdmissibilityConfig&  config
    ) noexcept;

}  // namespace timetable::domain::assignment
