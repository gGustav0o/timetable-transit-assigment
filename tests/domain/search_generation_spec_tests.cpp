#include <optional>
#include <limits>
#include <vector>

#include <gtest/gtest.h>

#include "timetable/domain/assignment/search/generation/branch_transition.hpp"
#include "timetable/domain/assignment/search/generation/successor.hpp"
#include "timetable/domain/assignment/search/scalar.hpp"

namespace timetable::domain::assignment {
namespace {

    TransferLimits transfer_limits() {
        return TransferLimits{
              .max_transfers     = TransferCount{ 3 }
            , .min_transfer_wait = Time{ 5.0 }
            , .max_transfer_wait = Time{ 20.0 }
        };
    }

    RouteSegment timed_route_segment() {
        return RouteSegment{
              .id       = RouteSegmentId{ 0 }
            , .length   = Length{ 1.0 }
            , .run_time = Time{ 10.0 }
            , .topology = LineRouteTopology{
                  .from  = StopOccurrence{ .stop = StopId{ 10 }, .position = RoutePosition{ 0 } }
                , .to    = StopOccurrence{ .stop = StopId{ 11 }, .position = RoutePosition{ 1 } }
                , .line  = LineId{ 1 }
                , .route = RouteId{ 1 }
              }
        };
    }

    ConnectionSegment timed_connection(std::int64_t id, double departure, double arrival) {
        return ConnectionSegment{
              .id            = ConnectionSegmentId{ id }
            , .route_segment = RouteSegmentId{ 0 }
            , .trip          = TripId{ id + 1 }
            , .from_index    = RoutePosition{ 0 }
            , .to_index      = RoutePosition{ 1 }
            , .departure     = Time{ departure }
            , .arrival       = Time{ arrival }
        };
    }

    PreprocessedNetwork timed_network() {
        return PreprocessedNetwork{
              .route_segments = { timed_route_segment() }
            , .connection_segments = {
                  timed_connection(0, 10.0, 20.0)
                , timed_connection(1, 18.0, 28.0)
                , timed_connection(2, 35.0, 45.0)
              }
            , .connection_index = preprocessing::ConnectionSegmentIndex{
                  .boarding_order = {
                        ConnectionSegmentId{ 0 }
                      , ConnectionSegmentId{ 1 }
                      , ConnectionSegmentId{ 2 }
                  }
                , .boarding_departures = {
                        Time{ 10.0 }
                      , Time{ 18.0 }
                      , Time{ 35.0 }
                  }
                , .boarding_stop_buckets = { StopId{ 10 } }
                , .boarding_offsets = { 0u, 3u }
              }
        };
    }

    SearchBranch preboarding_branch() {
        return SearchBranch{
            .trace = SearchPartialTrace{
                  .origin           = ZoneId{ 1 }
                , .current_physical = endpoint_key(StopId{ 10 })
                , .phase            = SearchBranchPhase::BeforeFirstBoarding
            }
          , .od_day_carrier = OdDayProductionCarrier{
                .path_identity = make_od_day_path_prefix(ZoneId{ 1 })
            }
        };
    }

}  // namespace

TEST(SearchScalarSpec, NonNegativeScalarRejectsNegativeAndNonFiniteValues) {
    EXPECT_TRUE(make_non_negative_scalar(0.0, "zero").has_value());
    EXPECT_TRUE(make_non_negative_scalar(2.5, "positive").has_value());
    EXPECT_FALSE(make_non_negative_scalar(-1.0, "negative").has_value());
    EXPECT_FALSE(make_non_negative_scalar(std::numeric_limits<double>::infinity(), "infinite").has_value());
}

TEST(SearchScalarSpec, PositiveScalarRejectsZero) {
    EXPECT_TRUE(make_positive_scalar(1.0, "positive").has_value());
    EXPECT_FALSE(make_positive_scalar(0.0, "zero").has_value());
}

TEST(SearchSuccessorSpec, TimedEnumerationReturnsDiagnosticsWithSuccessorSet) {
    const auto network = timed_network();

    const auto result = collect_timed_connection_successors(
          network
        , endpoint_key(StopId{ 10 })
        , Time{ 12.0 }
        , transfer_limits()
        , nullptr
    );

    ASSERT_EQ(result.successors.size(), 1u);
    EXPECT_EQ(result.successors[0], ConnectionSegmentId{ 1 });
    EXPECT_EQ(result.diagnostics.scanned_windows, 1u);
    EXPECT_EQ(result.diagnostics.emitted_successors, 1u);
}

TEST(SearchSuccessorSpec, PaperGenerationReturnsDiagnosticsWithoutRuntimeStats) {
    const auto network = timed_network();
    const auto branch = preboarding_branch();
    const auto first_departure_domain = SearchTimeDomain{
        .windows = {
            SearchTimeWindow{
                  .begin = Time{ 12.0 }
                , .end   = Time{ 30.0 }
            }
        }
    };
    auto diagnostics = PaperSuccessorGenerationDiagnostics{};
    std::vector<SearchSuccessor> successors;

    for_each_paper_successor(
          network
        , ZoneId{ 1 }
        , ActiveDestinationMembership{}
        , branch
        , transfer_limits()
        , &first_departure_domain
        , &diagnostics
        , [&](SearchSuccessor successor) {
              successors.push_back(std::move(successor));
          }
    );

    ASSERT_EQ(successors.size(), 1u);
    EXPECT_EQ(successors[0].connection, ConnectionSegmentId{ 1 });
    EXPECT_EQ(diagnostics.walk_lookup.skipped_by_phase, 1u);
    EXPECT_EQ(diagnostics.timed_lookup_skipped_phase, 0u);
}

TEST(SearchBranchTransitionSpec, RejectsTimedTransitionFromAtOriginAsValue) {
    const auto network = timed_network();
    BranchArena branches;
    auto branch = preboarding_branch();
    branch.trace.phase = SearchBranchPhase::AtOrigin;
    branch.trace.current_physical = endpoint_key(ZoneId{ 1 });
    const auto root = append_branch(branches, branch);

    auto result = transition_search_branch_with_diagnostics(
          branches
        , root
        , branch_at(branches, root)
        , network
        , SearchSuccessor{ .connection = ConnectionSegmentId{ 0 } }
        , std::nullopt
        , SearchCostContext{}
    );

    ASSERT_TRUE(result.has_value()) << result.error().message();
    EXPECT_FALSE(result->branch.has_value());
    EXPECT_EQ(result->diagnostics.rejection, BranchTransitionRejection::InvalidTimedPhase);
}

TEST(SearchBranchTransitionSpec, PaperPrefixEvaluationBuildsNodeLocalCandidateMetrics) {
    const auto network = timed_network();
    const auto branch = preboarding_branch();
    BranchArena branches;
    append_branch(branches, branch);

    const auto result = evaluate_paper_connection_prefix_before_branch(
          branches
        , branch
        , network
        , SearchSuccessor{ .connection = ConnectionSegmentId{ 1 } }
        , std::nullopt
        , SearchCostContext{}
    );

    ASSERT_TRUE(result.has_value()) << result.error().message();
    EXPECT_TRUE(result->accepted());
    ASSERT_TRUE(result->connection_candidate.has_value());
    EXPECT_EQ(result->connection_candidate->node.physical, endpoint_key(StopId{ 11 }));
    EXPECT_EQ(result->connection_candidate->metrics.departure, Time{ 18.0 });
    EXPECT_EQ(result->connection_candidate->metrics.arrival, Time{ 28.0 });
    EXPECT_EQ(result->connection_candidate->metrics.journey_time, Time{ 10.0 });
    EXPECT_EQ(result->connection_candidate->metrics.transfers, TransferCount{ 0 });
}

TEST(SearchBranchTransitionSpec, PaperSuccessorFeasibilityReportsTimeDomainRejectionAsValue) {
    const auto network = timed_network();
    const auto branch = preboarding_branch();
    const auto first_departure_domain = SearchTimeDomain{
        .windows = {
            SearchTimeWindow{
                  .begin = Time{ 30.0 }
                , .end   = Time{ 40.0 }
            }
        }
    };

    const auto decision = evaluate_paper_search_successor_feasibility(
          branch
        , network
        , SearchSuccessor{ .connection = ConnectionSegmentId{ 0 } }
        , &first_departure_domain
        , transfer_limits()
    );

    EXPECT_FALSE(decision.accepted());
    EXPECT_EQ(
          decision.rejection
        , PaperSuccessorFeasibilityRejection::FirstDepartureDomain
    );
}

}  // namespace timetable::domain::assignment
