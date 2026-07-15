#include <vector>

#include <gtest/gtest.h>

#include <mathfp/types/units.hpp>

#include "search_test_support.hpp"

#include "timetable/domain/assignment/search/policy.hpp"
#include "timetable/domain/assignment/search/problem.hpp"
#include "timetable/domain/assignment/search/kernel.hpp"

namespace timetable::domain::assignment {
namespace {

    using test_support::domain;

    SearchTolerances permissive_search_tolerances() {
        return SearchTolerances{
              .imp_mult = Dimless{ 2.0 }
            , .imp_add  = Dimless{ 60.0 }
            , .jt_mult  = Dimless{ 2.0 }
            , .jt_add   = Dimless{ 60.0 }
            , .nt_mult  = Dimless{ 2.0 }
            , .nt_add   = Dimless{ 2.0 }
        };
    }

    TransferLimits transfer_limits() {
        return TransferLimits{
              .max_transfers     = TransferCount{ 3 }
            , .min_transfer_wait = Time{ 0.0 }
            , .max_transfer_wait = Time{ 600.0 }
            , .allow_start_wait  = true
            , .allow_end_wait    = true
        };
    }

    SearchParams search_params() {
        SearchParams params;
        params.transfers = transfer_limits();
        params.search_tolerances = permissive_search_tolerances();
        return params;
    }

    SearchCostContext search_cost() {
        auto result = make_base_search_cost_context(
              SearchImpedance{}
            , 1.0
        );
        if (!result) {
            ADD_FAILURE() << result.error().message();
            return SearchCostContext{};
        }
        return *result;
    }

    PreprocessedNetwork minimal_network() {
        return PreprocessedNetwork{
              .route_segments = {
                  RouteSegment{
                      .id = RouteSegmentId{ 0 }
                    , .length = Length{ 1.0 }
                    , .run_time = Time{ 1.0 }
                    , .topology = WalkRouteTopology{
                          .from = WalkEndpoint{ ZoneId{ 1 } }
                        , .to = WalkEndpoint{ StopId{ 10 } }
                      }
                  }
              }
            , .connection_segments = {
                  ConnectionSegment{
                      .id = ConnectionSegmentId{ 0 }
                    , .route_segment = RouteSegmentId{ 0 }
                  }
              }
        };
    }

}  // namespace

TEST(SearchKernelContract, ProblemRejectsEmptyDestinationSet) {
    const auto problem = make_search_problem(
          ZoneId{ 1 }
        , domain(0.0, 100.0)
        , std::vector<ZoneId>{}
    );

    EXPECT_FALSE(problem.has_value());
}

TEST(SearchKernelContract, ProblemRejectsDuplicateDestinations) {
    const auto problem = make_search_problem(
          ZoneId{ 1 }
        , domain(0.0, 100.0)
        , std::vector<ZoneId>{ ZoneId{ 2 }, ZoneId{ 2 } }
    );

    EXPECT_FALSE(problem.has_value());
}

TEST(SearchKernelContract, ProblemAcceptsOnlyKernelInputs) {
    const auto problem = make_search_problem(
          ZoneId{ 1 }
        , domain(0.0, 100.0)
        , std::vector<ZoneId>{ ZoneId{ 2 }, ZoneId{ 3 } }
    );

    ASSERT_TRUE(problem.has_value()) << problem.error().message();
    EXPECT_EQ(problem->origin, ZoneId{ 1 });
    ASSERT_EQ(problem->destinations.size(), 2u);
    EXPECT_EQ(problem->destinations[0], ZoneId{ 2 });
    EXPECT_EQ(problem->destinations[1], ZoneId{ 3 });
}

TEST(SearchKernelContract, PolicyIsMathematicalSearchPolicyOnly) {
    const auto params = search_params();
    const auto policy = make_search_policy(search_cost(), params);

    ASSERT_TRUE(policy.has_value()) << policy.error().message();
    EXPECT_EQ(policy->temporal.transfer_limits.max_transfers, TransferCount{ 3 });
    EXPECT_EQ(
          policy->relevance.exact.contract
        , ExactDominanceContract::ExtensionSafeCurrentState
    );
    EXPECT_DOUBLE_EQ(
          mathfp::units::as_dimless(policy->tolerance.tolerances.imp_mult)
        , 2.0
    );
    EXPECT_EQ(
          policy->loops.transfer_policy
        , SearchLoopTransferPolicy::SameLineReboardingOnlyForTimeSavingLoop
    );
}

TEST(SearchKernelContract, PolicyRejectsInvalidTransferWindow) {
    auto params = search_params();
    params.transfers.min_transfer_wait = Time{ 700.0 };
    params.transfers.max_transfer_wait = Time{ 600.0 };

    const auto policy = make_search_policy(search_cost(), params);

    EXPECT_FALSE(policy.has_value());
}

TEST(SearchKernelContract, KernelInputIsProblemPolicyAndPreprocessedNetwork) {
    auto network = minimal_network();
    auto problem = make_search_problem(
          ZoneId{ 1 }
        , domain(0.0, 100.0)
        , std::vector<ZoneId>{ ZoneId{ 2 } }
    );
    auto policy = make_search_policy(search_cost(), search_params());

    ASSERT_TRUE(problem.has_value()) << problem.error().message();
    ASSERT_TRUE(policy.has_value()) << policy.error().message();

    auto input = make_search_kernel_input(
          network
        , std::move(*problem)
        , std::move(*policy)
    );

    ASSERT_TRUE(input.has_value()) << input.error().message();
    EXPECT_EQ(input->network.get().connection_segments.size(), 1u);
    EXPECT_EQ(input->problem.origin, ZoneId{ 1 });
}

}  // namespace timetable::domain::assignment
