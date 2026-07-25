#include <gtest/gtest.h>

#include "timetable/domain/assignment/search/residual/validation.hpp"

namespace timetable::domain::assignment {
namespace {

    ResidualReachabilityKey completed_state(
        TransferCount remaining
    ) {
        return ResidualReachabilityKey{
              .current_physical    = endpoint_key(ZoneId{ 2 })
            , .phase               = SearchBranchPhase::Completed
            , .remaining_transfers = remaining
        };
    }

    ResidualSuffixLowerBounds finite_lower_bounds() {
        return ResidualSuffixLowerBounds{
              .journey_time = Time{ 0.0 }
            , .transfers    = TransferCount{ 0 }
            , .impedance    = 0.0
        };
    }

    DestinationResidualReachability valid_destination_reachability(
        TransferCount max_transfers
    ) {
        auto reachability = DestinationResidualReachability{};

        auto remaining = TransferCount{ 0 };
        while (remaining <= max_transfers) {
            const auto state = completed_state(remaining);
            reachability.reachable_states.insert(state);
            reachability.suffix_lower_bounds.emplace(state, finite_lower_bounds());

            const auto next = bounded_next_transfer_count(remaining, max_transfers);
            if (!next.has_value()) {
                break;
            }
            remaining = *next;
        }

        return reachability;
    }

}  // namespace

TEST(ResidualValidation, AcceptsCanonicalDestinationSeedsAndBounds) {
    const auto reachability = valid_destination_reachability(TransferCount{ 1 });

    EXPECT_TRUE(validate_destination_residual_reachability(
          ZoneId{ 2 }
        , reachability
        , TransferCount{ 1 }
    ).has_value());
}

TEST(ResidualValidation, RejectsMissingDestinationCompletionSeed) {
    auto reachability = valid_destination_reachability(TransferCount{ 1 });
    reachability.reachable_states.erase(completed_state(TransferCount{ 1 }));
    reachability.suffix_lower_bounds.erase(completed_state(TransferCount{ 1 }));

    EXPECT_FALSE(validate_destination_residual_reachability(
          ZoneId{ 2 }
        , reachability
        , TransferCount{ 1 }
    ).has_value());
}

TEST(ResidualValidation, RejectsStateOutsideTransferBudget) {
    auto reachability = valid_destination_reachability(TransferCount{ 1 });
    const auto invalid_state = completed_state(TransferCount{ 2 });
    reachability.reachable_states.insert(invalid_state);
    reachability.suffix_lower_bounds.emplace(invalid_state, finite_lower_bounds());

    EXPECT_FALSE(validate_destination_residual_reachability(
          ZoneId{ 2 }
        , reachability
        , TransferCount{ 1 }
    ).has_value());
}

TEST(ResidualValidation, RejectsStopPhaseOutsideStop) {
    auto reachability = valid_destination_reachability(TransferCount{ 0 });
    const auto invalid_state = ResidualReachabilityKey{
          .current_physical    = endpoint_key(ZoneId{ 1 })
        , .phase               = SearchBranchPhase::BeforeFirstBoarding
        , .remaining_transfers = TransferCount{ 0 }
    };
    reachability.reachable_states.insert(invalid_state);
    reachability.suffix_lower_bounds.emplace(invalid_state, finite_lower_bounds());

    EXPECT_FALSE(validate_destination_residual_reachability(
          ZoneId{ 2 }
        , reachability
        , TransferCount{ 0 }
    ).has_value());
}

TEST(ResidualValidation, RejectsReachableStateWithoutLowerBounds) {
    auto reachability = valid_destination_reachability(TransferCount{ 0 });
    reachability.reachable_states.insert(ResidualReachabilityKey{
          .current_physical    = endpoint_key(ZoneId{ 1 })
        , .phase               = SearchBranchPhase::AtOrigin
        , .remaining_transfers = TransferCount{ 0 }
    });

    EXPECT_FALSE(validate_destination_residual_reachability(
          ZoneId{ 2 }
        , reachability
        , TransferCount{ 0 }
    ).has_value());
}

TEST(ResidualValidation, RejectsInvalidLowerBoundValue) {
    auto reachability = valid_destination_reachability(TransferCount{ 0 });
    reachability.suffix_lower_bounds[completed_state(TransferCount{ 0 })] =
        ResidualSuffixLowerBounds{
              .journey_time = Time{ -1.0 }
            , .transfers    = TransferCount{ 0 }
            , .impedance    = 0.0
        };

    EXPECT_FALSE(validate_destination_residual_reachability(
          ZoneId{ 2 }
        , reachability
        , TransferCount{ 0 }
    ).has_value());
}

TEST(ResidualValidation, RejectsLowerBoundForUnreachableState) {
    auto reachability = valid_destination_reachability(TransferCount{ 0 });
    reachability.suffix_lower_bounds.emplace(
          ResidualReachabilityKey{
                .current_physical    = endpoint_key(ZoneId{ 1 })
              , .phase               = SearchBranchPhase::AtOrigin
              , .remaining_transfers = TransferCount{ 0 }
            }
        , finite_lower_bounds()
    );

    EXPECT_FALSE(validate_destination_residual_reachability(
          ZoneId{ 2 }
        , reachability
        , TransferCount{ 0 }
    ).has_value());
}

TEST(ResidualValidation, ValidatesEveryDestination) {
    auto reachability = ResidualReachability{};
    reachability.destinations.emplace(
          ZoneId{ 2 }
        , valid_destination_reachability(TransferCount{ 0 })
    );
    reachability.destinations.emplace(ZoneId{ 3 }, DestinationResidualReachability{});

    EXPECT_FALSE(validate_residual_reachability(
          reachability
        , TransferCount{ 0 }
    ).has_value());
}

}  // namespace timetable::domain::assignment
