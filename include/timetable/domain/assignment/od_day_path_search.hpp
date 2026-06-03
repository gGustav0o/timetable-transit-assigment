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
        ConnectionSegmentWitness
    };

    enum class OdDayPathSuccessorExpansionPolicy : std::uint8_t {
        ConnectionSegmentSuccessors
    };

    /**
     * @brief Paper-level successor contract for production OD-day search.
     *
     * A successor generator may emit only connection segments that can be
     * inserted into the connection tree with respect to branch phase, temporal
     * suitability and the hard transfer bound. Later validation is retained as
     * a defensive invariant, not as the primary filtering stage.
     */
    enum class OdDayPathSuccessorContract : std::uint8_t {
        InsertableConnectionSegments
    };

    enum class OdDayPathSupplyGraphPolicy : std::uint8_t {
        PreprocessedConnectionSegmentIndex
    };

    enum class OdDayPathProductionCarrierPolicy : std::uint8_t {
        CompactConnectionSegmentPrefix
    };

    enum class OdDayPathFrontierPolicy : std::uint8_t {
        ConnectionTreeLevelQueues
    };

    enum class OdDayPathTreeLabelPolicy : std::uint8_t {
        NodeLocalKnownConnections
    };

    enum class OdDayPathBoundLayerPolicy : std::uint8_t {
        PaperNodeLocalConnectionSets
    };

    enum class OdDayPathResultProjectionPolicy : std::uint8_t {
        DayPathPostLayer
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
     * Production search follows the paper's connection-tree semantics:
     * connection segments are concatenated only when temporal suitability,
     * node-local relevance and node-local tolerances hold. DayPath is a result
     * projection over completed relevant connections, not the tree carrier.
     */
    enum class OdDayPathFeasibilitySemantics : std::uint8_t {
        TimetableTemporalSuitability
    };

    struct OdDayPathSearchContract final {
        OdDayPathSearchHorizon        horizon{ OdDayPathSearchHorizon::ServiceDay };
        OdDayPathTreeContract         tree_contract{ OdDayPathTreeContract::OneTreePerDeclaredOrigin };
        OdDayPathDemandIntervalPolicy demand_interval_policy{
            OdDayPathDemandIntervalPolicy::AssignmentOnly
        };
        OdDayPathTimedSupportPolicy   timed_support_policy{
            OdDayPathTimedSupportPolicy::ConnectionSegmentWitness
        };
        OdDayPathSuccessorExpansionPolicy successor_expansion_policy{
            OdDayPathSuccessorExpansionPolicy::ConnectionSegmentSuccessors
        };
        OdDayPathSuccessorContract successor_contract{
            OdDayPathSuccessorContract::InsertableConnectionSegments
        };
        OdDayPathSupplyGraphPolicy supply_graph_policy{
            OdDayPathSupplyGraphPolicy::PreprocessedConnectionSegmentIndex
        };
        OdDayPathProductionCarrierPolicy production_carrier_policy{
            OdDayPathProductionCarrierPolicy::CompactConnectionSegmentPrefix
        };
        OdDayPathFrontierPolicy frontier_policy{
            OdDayPathFrontierPolicy::ConnectionTreeLevelQueues
        };
        OdDayPathTreeLabelPolicy tree_label_policy{
            OdDayPathTreeLabelPolicy::NodeLocalKnownConnections
        };
        OdDayPathBoundLayerPolicy bound_layer_policy{
            OdDayPathBoundLayerPolicy::PaperNodeLocalConnectionSets
        };
        OdDayPathResultProjectionPolicy result_projection_policy{
            OdDayPathResultProjectionPolicy::DayPathPostLayer
        };
        OdDayPathAlternativeRetentionPolicy alternative_retention_policy{
            OdDayPathAlternativeRetentionPolicy::ProductionOdPairSlotsOnly
        };
        OdDayPathSignaturePolicy signature_policy{
            OdDayPathSignaturePolicy::RouteStopLinePattern
        };
        OdDayPathFeasibilitySemantics feasibility_semantics{
            OdDayPathFeasibilitySemantics::TimetableTemporalSuitability
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
            , .timed_support_policy   = OdDayPathTimedSupportPolicy::ConnectionSegmentWitness
            , .successor_expansion_policy =
                OdDayPathSuccessorExpansionPolicy::ConnectionSegmentSuccessors
            , .successor_contract =
                OdDayPathSuccessorContract::InsertableConnectionSegments
            , .supply_graph_policy =
                OdDayPathSupplyGraphPolicy::PreprocessedConnectionSegmentIndex
            , .production_carrier_policy =
                OdDayPathProductionCarrierPolicy::CompactConnectionSegmentPrefix
            , .frontier_policy =
                OdDayPathFrontierPolicy::ConnectionTreeLevelQueues
            , .tree_label_policy =
                OdDayPathTreeLabelPolicy::NodeLocalKnownConnections
            , .bound_layer_policy =
                OdDayPathBoundLayerPolicy::PaperNodeLocalConnectionSets
            , .result_projection_policy =
                OdDayPathResultProjectionPolicy::DayPathPostLayer
            , .alternative_retention_policy =
                OdDayPathAlternativeRetentionPolicy::ProductionOdPairSlotsOnly
            , .signature_policy =
                OdDayPathSignaturePolicy::RouteStopLinePattern
            , .feasibility_semantics   =
                OdDayPathFeasibilitySemantics::TimetableTemporalSuitability
            , .declared_origin_count  = declared_origin_count
        };
    }

    [[nodiscard]] inline constexpr bool satisfies_od_day_path_search_contract(
        const OdDayPathSearchContract& contract
    ) noexcept {
        return contract.horizon == OdDayPathSearchHorizon::ServiceDay
            && contract.tree_contract == OdDayPathTreeContract::OneTreePerDeclaredOrigin
            && contract.demand_interval_policy == OdDayPathDemandIntervalPolicy::AssignmentOnly
            && contract.timed_support_policy == OdDayPathTimedSupportPolicy::ConnectionSegmentWitness
            && contract.successor_expansion_policy
                == OdDayPathSuccessorExpansionPolicy::ConnectionSegmentSuccessors
            && contract.successor_contract
                == OdDayPathSuccessorContract::InsertableConnectionSegments
            && contract.supply_graph_policy
                == OdDayPathSupplyGraphPolicy::PreprocessedConnectionSegmentIndex
            && contract.production_carrier_policy
                == OdDayPathProductionCarrierPolicy::CompactConnectionSegmentPrefix
            && contract.frontier_policy
                == OdDayPathFrontierPolicy::ConnectionTreeLevelQueues
            && contract.tree_label_policy
                == OdDayPathTreeLabelPolicy::NodeLocalKnownConnections
            && contract.bound_layer_policy
                == OdDayPathBoundLayerPolicy::PaperNodeLocalConnectionSets
            && contract.result_projection_policy
                == OdDayPathResultProjectionPolicy::DayPathPostLayer
            && contract.alternative_retention_policy
                == OdDayPathAlternativeRetentionPolicy::ProductionOdPairSlotsOnly
            && contract.signature_policy
                == OdDayPathSignaturePolicy::RouteStopLinePattern
            && contract.feasibility_semantics
                == OdDayPathFeasibilitySemantics::TimetableTemporalSuitability
            && contract.declared_origin_count > 0u;
    }

    /**
     * @brief Node of the day-level supply graph used by structural OD search.
     *
     * endpoint is the physical stop/zone key. Production OD-day supply graphs
     * use the same normalized identity as DayPathSignature, so occurrence is
     * normally empty there; concrete repeated-stop support remains a timed
     * support-label property rather than a public path node.
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
