#include "timetable/domain/assignment/search/policy.hpp"

#include <cstdint>
#include <utility>

#include <mathfp/core/error.hpp>
#include <mathfp/core/try.hpp>
#include <mathfp/types/units.hpp>

#include "timetable/domain/assignment/search/scalar.hpp"

namespace timetable::domain::assignment {
    namespace {

        [[nodiscard]] mathfp::Expected<mathfp::Unit> validate_transfer_limits(
            const TransferLimits& limits
        ) {
            if (limits.max_transfers.get() < 0) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("search policy max_transfers must be non-negative")
                        .ctx("max_transfers", limits.max_transfers.get())
                );
            }
            MATHFP_TRY(validate_non_negative_scalar(
                  limits.min_transfer_wait.value()
                , "min_transfer_wait"
            ));
            MATHFP_TRY(validate_non_negative_scalar(
                  limits.max_transfer_wait.value()
                , "max_transfer_wait"
            ));
            if (limits.min_transfer_wait.value() > limits.max_transfer_wait.value()) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("search policy transfer wait interval is empty")
                        .ctx("min_transfer_wait", limits.min_transfer_wait.value())
                        .ctx("max_transfer_wait", limits.max_transfer_wait.value())
                );
            }
            return mathfp::kUnit;
        }

        [[nodiscard]] mathfp::Expected<mathfp::Unit> validate_tolerances(
            const SearchTolerances& tolerances
        ) {
            MATHFP_TRY(validate_non_negative_scalar(
                  tolerances.imp_mult.value()
                , "imp_mult"
            ));
            MATHFP_TRY(validate_non_negative_scalar(
                  tolerances.imp_add.value()
                , "imp_add"
            ));
            MATHFP_TRY(validate_non_negative_scalar(
                  tolerances.jt_mult.value()
                , "jt_mult"
            ));
            MATHFP_TRY(validate_non_negative_scalar(
                  tolerances.jt_add.seconds()
                , "jt_add"
            ));
            MATHFP_TRY(validate_non_negative_scalar(
                  tolerances.nt_mult.value()
                , "nt_mult"
            ));
            MATHFP_TRY(validate_non_negative_scalar(
                  tolerances.nt_add.value()
                , "nt_add"
            ));
            return mathfp::kUnit;
        }

    }  // namespace

    mathfp::Expected<mathfp::Unit> validate_search_temporal_suitability_policy(
        const SearchTemporalSuitabilityPolicy& policy
    ) {
        return validate_transfer_limits(policy.transfer_limits);
    }

    mathfp::Expected<mathfp::Unit> validate_search_node_relevance_policy(
        const SearchNodeRelevancePolicy& policy
    ) {
        switch (policy.exact.contract) {
            case ExactDominanceContract::ExtensionSafeCurrentState:
                return mathfp::kUnit;
        }

        return mathfp::unexpected(
            mathfp::invalid_arg("unknown exact dominance contract")
                .ctx(
                      "contract"
                    , static_cast<std::int64_t>(policy.exact.contract)
                )
        );
    }

    mathfp::Expected<mathfp::Unit> validate_search_node_tolerance_policy(
        const SearchNodeTolerancePolicy& policy
    ) {
        return validate_tolerances(policy.tolerances);
    }

    mathfp::Expected<mathfp::Unit> validate_search_loop_policy(
        const SearchLoopPolicy& policy
    ) {
        switch (policy.transfer_policy) {
            case SearchLoopTransferPolicy::SameLineReboardingOnlyForTimeSavingLoop:
                return mathfp::kUnit;
        }

        return mathfp::unexpected(
            mathfp::invalid_arg("unknown search loop transfer policy")
                .ctx(
                      "transfer_policy"
                    , static_cast<std::int64_t>(policy.transfer_policy)
                )
        );
    }

    mathfp::Expected<mathfp::Unit> validate_search_policy(
        const SearchPolicy& policy
    ) {
        MATHFP_TRY(validate_search_cost_context(policy.cost));
        MATHFP_TRY(validate_search_temporal_suitability_policy(policy.temporal));
        MATHFP_TRY(validate_search_node_relevance_policy(policy.relevance));
        MATHFP_TRY(validate_search_node_tolerance_policy(policy.tolerance));
        MATHFP_TRY(validate_search_loop_policy(policy.loops));
        return mathfp::kUnit;
    }

    mathfp::Expected<SearchPolicy> make_search_policy(
          SearchCostContext cost
        , const SearchParams& params
    ) {
        SearchPolicy policy{
              .cost = std::move(cost)
            , .temporal = SearchTemporalSuitabilityPolicy{
                  .transfer_limits = params.transfers
              }
            , .relevance = SearchNodeRelevancePolicy{}
            , .tolerance = SearchNodeTolerancePolicy{
                  .tolerances = params.search_tolerances
              }
            , .loops = SearchLoopPolicy{}
        };
        MATHFP_TRY(validate_search_policy(policy));
        return policy;
    }

}  // namespace timetable::domain::assignment
