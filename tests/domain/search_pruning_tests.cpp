#include <algorithm>
#include <span>

#include <gtest/gtest.h>

#include "timetable/domain/assignment/search/frontier/retention_operations.hpp"
#include "timetable/domain/assignment/search/relations/paper_connection_relevance.hpp"
#include "timetable/domain/assignment/search_pruning.hpp"
#include "timetable/domain/assignment/search_pruning_plan.hpp"

namespace timetable::domain::assignment {
namespace {

    SearchPruningMetrics metrics(
          double departure
        , double arrival
        , double journey_time
        , int    transfers
        , double impedance
        , double fare = 0.0
        , double walk_time = 0.0
    ) {
        return SearchPruningMetrics{
              .departure   = Time{ departure }
            , .arrival     = Time{ arrival }
            , .journey_time = Time{ journey_time }
            , .walk_time    = Time{ walk_time }
            , .transfers    = TransferCount{ transfers }
            , .fare         = fare
            , .impedance    = impedance
        };
    }

    SearchTolerances tolerances(
          double imp_mult
        , double imp_add
        , double jt_mult
        , double jt_add
        , double nt_mult
        , double nt_add
    ) {
        return SearchTolerances{
              .imp_mult = Dimless{ imp_mult }
            , .imp_add  = Dimless{ imp_add }
            , .jt_mult  = Dimless{ jt_mult }
            , .jt_add   = Dimless{ jt_add }
            , .nt_mult  = Dimless{ nt_mult }
            , .nt_add   = Dimless{ nt_add }
        };
    }

}  // namespace

TEST(SearchPruningExactDominance, MatchesPaperRelevanceCoordinates) {
    const auto existing = metrics(
          20.0  // later departure is better
        , 50.0  // earlier arrival is better
        , 30.0
        , 1
        , 42.0
    );
    const auto candidate = metrics(
          10.0
        , 60.0
        , 50.0
        , 2
        , 42.0
    );

    EXPECT_TRUE(dominates_exactly(existing, candidate));
    EXPECT_FALSE(is_exactly_relevant(
          candidate
        , std::span<const SearchPruningMetrics>{ &existing, 1u }
    ));
}

TEST(SearchPruningExactDominance, EqualMetricsAreTiesNotDominance) {
    const auto a = metrics(10.0, 40.0, 30.0, 1, 50.0);
    const auto b = metrics(10.0, 40.0, 30.0, 1, 50.0);

    EXPECT_FALSE(dominates_exactly(a, b));
    EXPECT_TRUE(is_exactly_relevant(
          b
        , std::span<const SearchPruningMetrics>{ &a, 1u }
    ));
}

TEST(SearchPruningExactDominance, EarlierDepartureCannotDominate) {
    const auto earlier_departure = metrics(5.0, 30.0, 25.0, 0, 10.0);
    const auto candidate = metrics(10.0, 40.0, 30.0, 1, 20.0);

    EXPECT_FALSE(dominates_exactly(earlier_departure, candidate));
}

TEST(SearchPruningMetricSet, InsertRemovesExactDominatedSuffixAndKeepsArrivalOrder) {
    SearchPruningMetricSet set;
    insert_exact_pruning_metrics_in_place(
          ExactPruningPolicy{}
        , set
        , metrics(0.0, 30.0, 30.0, 1, 30.0)
    );
    insert_exact_pruning_metrics_in_place(
          ExactPruningPolicy{}
        , set
        , metrics(0.0, 50.0, 50.0, 2, 50.0)
    );

    insert_exact_pruning_metrics_in_place(
          ExactPruningPolicy{}
        , set
        , metrics(10.0, 40.0, 30.0, 1, 25.0)
    );

    ASSERT_EQ(set.metrics.size(), 2u);
    EXPECT_DOUBLE_EQ(set.metrics[0].arrival.value(), 30.0);
    EXPECT_DOUBLE_EQ(set.metrics[1].arrival.value(), 40.0);
    EXPECT_DOUBLE_EQ(set.summary.min_impedance, 25.0);
    const auto dominated = metrics(0.0, 50.0, 50.0, 2, 50.0);
    EXPECT_TRUE(std::none_of(
          set.metrics.begin()
        , set.metrics.end()
        , [&](const SearchPruningMetrics& retained) {
              return retained.departure == dominated.departure
                  && retained.arrival == dominated.arrival
                  && retained.transfers == dominated.transfers
                  && retained.impedance == dominated.impedance;
          }
    ));
    EXPECT_TRUE(validate_search_pruning_metric_set(set).has_value());
}

TEST(SearchPruningApproximateRetention, AppliesPaperToleranceInequalitiesAndTransferLimit) {
    const auto known = metrics(0.0, 40.0, 40.0, 1, 100.0);
    const auto summary = summarize_pruning_metrics(
        std::span<const SearchPruningMetrics>{ &known, 1u }
    );
    const auto policy = ApproximatePruningPolicy{
        .tolerances = tolerances(1.2, 5.0, 1.5, 0.0, 2.0, 0.0)
    };
    const auto limits = TransferLimits{
          .max_transfers     = TransferCount{ 3 }
        , .min_transfer_wait = Time{ 0.0 }
        , .max_transfer_wait = Time{ 600.0 }
    };

    EXPECT_TRUE(within_approximate_retention(
          metrics(0.0, 60.0, 60.0, 2, 125.0)
        , summary
        , policy
        , limits
    ));
    EXPECT_FALSE(within_approximate_retention(
          metrics(0.0, 80.0, 80.0, 2, 125.0)
        , summary
        , policy
        , limits
    ));
    EXPECT_FALSE(within_approximate_retention(
          metrics(0.0, 60.0, 60.0, 4, 125.0)
        , summary
        , policy
        , limits
    ));
}

TEST(ConnectionSetCy, RelevanceOnlyConsidersKnownArrivalsNotLaterThanCandidate) {
    SearchPruningExecutionPlan execution;
    execution.exact_enabled = true;
    execution.approximate_enabled = false;
    execution.approximate_policy = std::nullopt;

    ConnectionSetCy set;
    std::vector<PaperConnectionLabelId> removed_labels;
    insert_paper_node_connection_metrics(
          execution
        , set
        , metrics(0.0, 40.0, 40.0, 2, 100.0)
        , PaperConnectionLabelId{ 1 }
        , removed_labels
    );
    insert_paper_node_connection_metrics(
          execution
        , set
        , metrics(20.0, 80.0, 60.0, 0, 10.0)
        , PaperConnectionLabelId{ 2 }
        , removed_labels
    );

    const auto candidate = metrics(10.0, 50.0, 40.0, 1, 50.0);

    EXPECT_TRUE(paper_node_connection_relevant(
          set
        , ExactPruningPolicy{}
        , candidate
    ));
}

TEST(ConnectionSetCy, RelevanceRejectsPaperDominatedCandidate) {
    SearchPruningExecutionPlan execution;
    execution.exact_enabled = true;
    execution.approximate_enabled = false;
    execution.approximate_policy = std::nullopt;

    ConnectionSetCy set;
    std::vector<PaperConnectionLabelId> removed_labels;
    insert_paper_node_connection_metrics(
          execution
        , set
        , metrics(20.0, 40.0, 30.0, 1, 50.0)
        , PaperConnectionLabelId{ 1 }
        , removed_labels
    );

    const auto candidate = metrics(10.0, 50.0, 40.0, 2, 60.0);

    EXPECT_FALSE(paper_node_connection_relevant(
          set
        , ExactPruningPolicy{}
        , candidate
    ));
}

TEST(ConnectionSetCy, InsertRemovesDominatedSuffixAndReportsLabels) {
    SearchPruningExecutionPlan execution;
    execution.exact_enabled = true;
    execution.approximate_enabled = false;
    execution.approximate_policy = std::nullopt;

    ConnectionSetCy set;
    std::vector<PaperConnectionLabelId> removed_labels;

    insert_paper_node_connection_metrics(
          execution
        , set
        , metrics(0.0, 30.0, 30.0, 0, 30.0)
        , PaperConnectionLabelId{ 1 }
        , removed_labels
    );
    insert_paper_node_connection_metrics(
          execution
        , set
        , metrics(0.0, 50.0, 50.0, 2, 50.0)
        , PaperConnectionLabelId{ 2 }
        , removed_labels
    );

    insert_paper_node_connection_metrics(
          execution
        , set
        , metrics(10.0, 40.0, 30.0, 1, 25.0)
        , PaperConnectionLabelId{ 3 }
        , removed_labels
    );

    ASSERT_EQ(set.size(), 2u);
    ASSERT_EQ(removed_labels.size(), 1u);
    EXPECT_EQ(removed_labels[0].value, 2u);
    EXPECT_DOUBLE_EQ(set.entries()[0].metrics.arrival.value(), 30.0);
    EXPECT_EQ(set.entries()[0].label.value, 1u);
    EXPECT_DOUBLE_EQ(set.entries()[1].metrics.arrival.value(), 40.0);
    EXPECT_EQ(set.entries()[1].label.value, 3u);
    EXPECT_DOUBLE_EQ(set.summary().min_impedance, 25.0);
}

}  // namespace timetable::domain::assignment
