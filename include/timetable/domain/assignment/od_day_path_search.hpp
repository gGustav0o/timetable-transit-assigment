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

    enum class OdDayPathSuccessorExpansionPolicy : std::uint8_t {
        StructuralSupplyEdges
    };

    enum class OdDayPathTreeLabelPolicy : std::uint8_t {
        SeparateFromOdAlternatives
    };

    enum class OdDayPathAlternativeRetentionPolicy : std::uint8_t {
        ProductionOdPairSlotsOnly
    };

    enum class OdDayPathSignaturePolicy : std::uint8_t {
        RouteStopLinePattern
    };

    /**
     * @brief Day-level path feasibility semantics.
     *
     * A structural OD-day path is feasible iff its structural leg sequence has
     * at least one chronologically feasible chain of concrete timetable support
     * labels. The public alternative remains day-level; the concrete labels are
     * search witnesses and representative/metric support, not OD alternatives.
     */
    enum class OdDayPathFeasibilitySemantics : std::uint8_t {
        ExistsTimedSupportLabelPath
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
        OdDayPathSuccessorExpansionPolicy successor_expansion_policy{
            OdDayPathSuccessorExpansionPolicy::StructuralSupplyEdges
        };
        OdDayPathTreeLabelPolicy tree_label_policy{
            OdDayPathTreeLabelPolicy::SeparateFromOdAlternatives
        };
        OdDayPathAlternativeRetentionPolicy alternative_retention_policy{
            OdDayPathAlternativeRetentionPolicy::ProductionOdPairSlotsOnly
        };
        OdDayPathSignaturePolicy signature_policy{
            OdDayPathSignaturePolicy::RouteStopLinePattern
        };
        OdDayPathFeasibilitySemantics feasibility_semantics{
            OdDayPathFeasibilitySemantics::ExistsTimedSupportLabelPath
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
            , .successor_expansion_policy =
                OdDayPathSuccessorExpansionPolicy::StructuralSupplyEdges
            , .tree_label_policy =
                OdDayPathTreeLabelPolicy::SeparateFromOdAlternatives
            , .alternative_retention_policy =
                OdDayPathAlternativeRetentionPolicy::ProductionOdPairSlotsOnly
            , .signature_policy =
                OdDayPathSignaturePolicy::RouteStopLinePattern
            , .feasibility_semantics   =
                OdDayPathFeasibilitySemantics::ExistsTimedSupportLabelPath
            , .declared_origin_count  = declared_origin_count
        };
    }

    [[nodiscard]] inline constexpr bool satisfies_od_day_path_search_contract(
        const OdDayPathSearchContract& contract
    ) noexcept {
        return contract.horizon == OdDayPathSearchHorizon::ServiceDay
            && contract.tree_contract == OdDayPathTreeContract::OneTreePerDeclaredOrigin
            && contract.demand_interval_policy == OdDayPathDemandIntervalPolicy::AssignmentOnly
            && contract.timed_support_policy == OdDayPathTimedSupportPolicy::SupportSetWithRepresentative
            && contract.successor_expansion_policy
                == OdDayPathSuccessorExpansionPolicy::StructuralSupplyEdges
            && contract.tree_label_policy
                == OdDayPathTreeLabelPolicy::SeparateFromOdAlternatives
            && contract.alternative_retention_policy
                == OdDayPathAlternativeRetentionPolicy::ProductionOdPairSlotsOnly
            && contract.signature_policy
                == OdDayPathSignaturePolicy::RouteStopLinePattern
            && contract.feasibility_semantics
                == OdDayPathFeasibilitySemantics::ExistsTimedSupportLabelPath
            && contract.declared_origin_count > 0u;
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

    /*
     * Production OD-day search results are declared in search/search.hpp as
     * OdDayPathSearchResult/OdDayOriginPathSet/OdDayPathPairResult. This header
     * owns the mathematical contract and day-level supply graph vocabulary only;
     * it deliberately does not introduce a parallel result hierarchy.
     */

}  // namespace timetable::domain::assignment
