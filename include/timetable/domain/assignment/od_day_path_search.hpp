#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include <mathfp/types/strong_type.hpp>

#include "timetable/domain/assignment/day_path.hpp"
#include "timetable/domain/model.hpp"
#include "timetable/domain/segments.hpp"

namespace timetable::domain::assignment {

    struct DayLevelSupplyNodeRefTag {};
    struct DayLevelSupplyEdgeRefTag {};

    using DayLevelSupplyNodeRef = mathfp::StrongType<
          std::int64_t
        , DayLevelSupplyNodeRefTag
        , mathfp::strong_detail::EqualityComparable
        , mathfp::strong_detail::Ordered
    >;

    using DayLevelSupplyEdgeRef = mathfp::StrongType<
          std::int64_t
        , DayLevelSupplyEdgeRefTag
        , mathfp::strong_detail::EqualityComparable
        , mathfp::strong_detail::Ordered
    >;

    /**
     * @brief Required computational contract for the production OD-day run.
     *
     * The demanded mode is not an interval-local or raw timed-connection search.
     * It builds exactly one search tree per declared origin for the full service
     * day, retains day-level OD path alternatives, and treats demand intervals
     * only as a later loading/split projection.
     */
    enum class OdDayPathSearchHorizon : std::uint8_t {
        ServiceDay
    };

    enum class OdDayPathTreeContract : std::uint8_t {
        OneTreePerDeclaredOrigin
    };

    enum class OdDayPathDemandIntervalPolicy : std::uint8_t {
        AssignmentOnly
    };

    enum class OdDayPathTimedSupportPolicy : std::uint8_t {
        SupportSetWithRepresentative
    };

    struct OdDayPathSearchContract final {
        OdDayPathSearchHorizon        horizon{ OdDayPathSearchHorizon::ServiceDay };
        OdDayPathTreeContract         tree_contract{ OdDayPathTreeContract::OneTreePerDeclaredOrigin };
        OdDayPathDemandIntervalPolicy demand_interval_policy{
            OdDayPathDemandIntervalPolicy::AssignmentOnly
        };
        OdDayPathTimedSupportPolicy   timed_support_policy{
            OdDayPathTimedSupportPolicy::SupportSetWithRepresentative
        };
        std::size_t                   declared_origin_count{};
    };

    [[nodiscard]] inline constexpr OdDayPathSearchContract make_od_day_path_search_contract(
        std::size_t declared_origin_count
    ) noexcept {
        return OdDayPathSearchContract{
              .horizon                = OdDayPathSearchHorizon::ServiceDay
            , .tree_contract          = OdDayPathTreeContract::OneTreePerDeclaredOrigin
            , .demand_interval_policy = OdDayPathDemandIntervalPolicy::AssignmentOnly
            , .timed_support_policy   = OdDayPathTimedSupportPolicy::SupportSetWithRepresentative
            , .declared_origin_count  = declared_origin_count
        };
    }

    [[nodiscard]] inline constexpr bool satisfies_od_day_path_search_contract(
        const OdDayPathSearchContract& contract
    ) noexcept {
        return contract.horizon == OdDayPathSearchHorizon::ServiceDay
            && contract.tree_contract == OdDayPathTreeContract::OneTreePerDeclaredOrigin
            && contract.demand_interval_policy == OdDayPathDemandIntervalPolicy::AssignmentOnly
            && contract.timed_support_policy == OdDayPathTimedSupportPolicy::SupportSetWithRepresentative;
    }

    /**
     * @brief Node of the day-level supply graph used by structural OD search.
     *
     * endpoint is the physical stop/zone key. occurrence is present only for
     * line-ride topology where repeated appearances of the same stop must be
     * distinguished; walking and zone nodes stay in physical endpoint space.
     */
    struct DayLevelSupplyNode final {
        DayLevelSupplyNodeRef           index{};
        EndpointKey                     endpoint{};
        std::optional<StopOccurrenceKey> occurrence{};

        auto operator<=>(const DayLevelSupplyNode&) const = default;
    };

    enum class DayLevelSupplyEdgeKind : std::uint8_t {
          AccessWalk
        , Ride
        , TransferWalk
        , EgressWalk
    };

    /**
     * @brief Timetable support summary for one structural day-level edge.
     *
     * A structural edge may be backed by many concrete connection segments
     * during the service day. The production OD-day path search must not expand
     * all of them as separate public alternatives; it keeps only their count and
     * one representative needed by cost/choice projections.
     */
    struct DayLevelTimedSupport final {
        std::size_t                        connection_count{};
        std::optional<ConnectionSegmentId> representative_connection_segment{};
        Time                               min_run_time{};
        Time                               representative_run_time{};
        double                             representative_fare{};

        auto operator<=>(const DayLevelTimedSupport&) const = default;
    };

    struct DayLevelSupplyEdge final {
        DayLevelSupplyEdgeRef index{};
        DayLevelSupplyEdgeKind kind{};
        DayLevelSupplyNodeRef from{};
        DayLevelSupplyNodeRef to{};
        DayPathLeg            structural_leg{};
        DayLevelTimedSupport  timed_support{};

        auto operator<=>(const DayLevelSupplyEdge&) const = default;
    };

    struct DayLevelSupplyGraph final {
        std::vector<DayLevelSupplyNode> nodes{};
        std::vector<DayLevelSupplyEdge> edges{};
        std::vector<std::vector<DayLevelSupplyEdgeRef>> outgoing_edges_by_node{};
    };

    /**
     * @brief Clock-free path retained by OD-day search.
     *
     * signature is the canonical OD path identity consumed by loading. supply_edges
     * is the graph trace that lets assignment project the path back to elementary
     * route segments without relying on raw timed connection enumeration.
     */
    struct DayStructuralPath final {
        DayPathSignature                   signature{};
        std::vector<DayLevelSupplyEdgeRef> supply_edges{};
    };

    /**
     * @brief One concrete timetable support selected for a structural path.
     *
     * The representative is auxiliary: it supports metrics, choice and
     * diagnostics, but it is not the identity of the OD-day alternative.
     */
    struct DayTimedRepresentative final {
        std::optional<SearchConnection> connection{};
        CompleteConnectionMetrics       complete_metrics{};
        ConnectionMetrics               connection_metrics{};
        std::size_t                     support_connection_count{};
    };

    struct OdDayPathAlternative final {
        DayStructuralPath      structural_path{};
        DayTimedRepresentative timed_representative{};
    };

    struct OdDayPathPairResult final {
        ZoneId                            origin{};
        ZoneId                            destination{};
        std::vector<OdDayPathAlternative> alternatives{};
    };

    struct OdDayOriginPathSet final {
        ZoneId                           origin{};
        std::vector<OdDayPathPairResult> pair_results{};
    };

    struct OdDayPathSearchSummary final {
        std::size_t origin_tree_count{};
        std::size_t od_pair_count{};
        std::size_t structural_path_count{};
        std::size_t representative_connection_count{};
    };

    struct OdDayPathSearchResult final {
        OdDayPathSearchContract         contract{};
        DayLevelSupplyGraph             supply_graph{};
        std::vector<OdDayOriginPathSet> origin_results{};
        OdDayPathSearchSummary          summary{};
    };

}  // namespace timetable::domain::assignment
