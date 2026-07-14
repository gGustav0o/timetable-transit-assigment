#pragma once

#include <cstddef>
#include <optional>

#include "timetable/domain/assignment/capacity.hpp"
#include "timetable/domain/assignment/od_day_path_contract.hpp"
#include "timetable/domain/assignment/search/model/support.hpp"
#include "timetable/domain/assignment/search/residual_reachability.hpp"
#include "timetable/domain/model.hpp"
#include "timetable/domain/segments.hpp"

namespace timetable::domain::assignment {

    /**
     * @brief Structural prefix of a connection explored by search.
     *
     * This is the trace state: current location, predecessor link and the
     * timed context needed to decide future feasible extensions.
     */
    struct SearchPartialTrace final {
        ZoneId                              origin;
        EndpointKey                         current_physical{};
        std::optional<StopOccurrenceKey>    current_occurrence{};
        SearchBranchPhase                   phase{ SearchBranchPhase::AtOrigin };
        std::optional<std::size_t>          parent_branch{};
        std::optional<ConnectionSegmentId>  incoming_segment{};
        std::optional<DayLevelSupplyEdgeRef> incoming_day_level_edge{};
        std::optional<DayLevelSupplyEdgeRef> last_day_level_ride_edge{};
        const ConnectionSegment*            last_timed_segment{};
        const RouteSegment*                 last_timed_route_segment{};
    };

    /**
     * @brief Incremental metrics of a partial connection prefix.
     *
     * The time, transfer and fare fields are parameter-independent base
     * metrics. capacity_exposure is a separate projection over a fixed
     * exogenous load snapshot for capacity-aware search; it is not a
     * post-assignment overload assessment.
     */
    struct SearchPartialMetrics final {
        std::optional<Time> departure{};
        std::optional<Time> current_time{};
        Time                access_time{};
        Time                in_vehicle_time{};
        Time                transfer_wait_time{};
        Time                transfer_walk_time{};
        Time                egress_time{};
        TransferCount       transfers{ TransferCount{0} };
        double              fare{};
        CapacityExposure    capacity_exposure{};
    };

    /**
     * Production OD-day carrier. It is self-contained by construction:
     * path_identity is the day-level alternative identity, support_prefix
     * is the compact timed/walk witness needed for metrics and split/load.
     */
    struct OdDayProductionCarrier final {
        OdDayPathPrefix      path_identity{};
        TimedSupportEnvelope support_envelope{};
        OdDaySupportPrefix   support_prefix{};
    };

    struct PaperConnectionLabelId final {
        std::size_t value{};

        bool operator==(const PaperConnectionLabelId&) const = default;
    };

    using PaperConnectionLabelVector = boost::container::small_vector<
          PaperConnectionLabelId
        , 1
    >;

    /**
     * One SearchBranch is one node of the dynamic multi-path connection tree.
     * Its incoming edge is a whole connection segment: access walk, timed ride,
     * transfer walk or egress walk.
     */
    struct SearchBranch final {
        SearchPartialTrace      trace{};
        SearchPartialMetrics    metrics{};
        OdDayProductionCarrier  od_day_carrier{};
        std::optional<PaperConnectionLabelId> paper_connection_label{};
    };

}  // namespace timetable::domain::assignment
