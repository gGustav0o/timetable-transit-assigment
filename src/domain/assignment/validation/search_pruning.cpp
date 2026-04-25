#include "timetable/domain/assignment/validation.hpp"

#include <mathfp/core/error.hpp>
#include <mathfp/core/unit.hpp>

namespace timetable::domain::assignment {

    mathfp::Expected<mathfp::Unit> validate_search_pruning_config(
        const SearchPruningConfig& config
    ) {
        if (config.model.requested_state_space != SearchPruningStateSpace::CurrentPhysicalOccurrenceAndTransferContext) {
            return mathfp::unexpected(
                mathfp::invalid_arg("search pruning config requests an unsupported state-space")
                    .ctx("requested_state_space", static_cast<std::int64_t>(config.model.requested_state_space))
            );
        }

        switch (config.runtime.rollout_stage) {
            case SearchPruningRolloutStage::Disabled:
                [[fallthrough]];
            case SearchPruningRolloutStage::ExactCurrentState:
                [[fallthrough]];
            case SearchPruningRolloutStage::ExactAndApproximateCurrentState:
                return mathfp::kUnit;
        }

        return mathfp::unexpected(
            mathfp::invalid_arg("search pruning config carries an unknown rollout stage")
                .ctx("rollout_stage", static_cast<std::int64_t>(config.runtime.rollout_stage))
        );
    }

    mathfp::Expected<mathfp::Unit> validate_search_pruning_execution_plan(
          const SearchPruningExecutionPlan& plan
        , const SearchTolerances&           tolerances
    ) {
        if (plan.state_space != SearchPruningStateSpace::CurrentPhysicalOccurrenceAndTransferContext) {
            return mathfp::unexpected(
                mathfp::internal_error("search pruning execution plan uses an unsupported state-space")
                    .ctx("state_space", static_cast<std::int64_t>(plan.state_space))
            );
        }

        if (plan.exact_policy.contract != ExactDominanceContract::ExtensionSafeCurrentState) {
            return mathfp::unexpected(
                mathfp::internal_error("search pruning execution plan carries an incompatible exact-dominance contract")
                    .ctx("exact_contract", static_cast<std::int64_t>(plan.exact_policy.contract))
            );
        }

        switch (plan.rollout_stage) {
            case SearchPruningRolloutStage::Disabled:
                if (plan.exact_enabled || plan.approximate_enabled || plan.approximate_policy.has_value()) {
                    return mathfp::unexpected(
                        mathfp::internal_error("disabled search pruning execution plan must not enable retention layers")
                    );
                }
                return mathfp::kUnit;

            case SearchPruningRolloutStage::ExactCurrentState:
                if (!plan.exact_enabled || plan.approximate_enabled || plan.approximate_policy.has_value()) {
                    return mathfp::unexpected(
                        mathfp::internal_error("exact-current-state pruning plan must enable only the exact layer")
                    );
                }
                return mathfp::kUnit;

            case SearchPruningRolloutStage::ExactAndApproximateCurrentState:
                if (!plan.exact_enabled || !plan.approximate_enabled || !plan.approximate_policy.has_value()) {
                    return mathfp::unexpected(
                        mathfp::internal_error("exact-and-approximate pruning plan must enable both retention layers")
                    );
                }

                if (
                       plan.approximate_policy->tolerances.imp_mult != tolerances.imp_mult
                    || plan.approximate_policy->tolerances.imp_add  != tolerances.imp_add
                    || plan.approximate_policy->tolerances.jt_mult  != tolerances.jt_mult
                    || plan.approximate_policy->tolerances.jt_add   != tolerances.jt_add
                    || plan.approximate_policy->tolerances.nt_mult  != tolerances.nt_mult
                    || plan.approximate_policy->tolerances.nt_add   != tolerances.nt_add
                ) {
                    return mathfp::unexpected(
                        mathfp::internal_error("search pruning execution plan tolerances disagree with search tolerances")
                    );
                }
                return mathfp::kUnit;
        }

        return mathfp::unexpected(
            mathfp::internal_error("search pruning execution plan carries an unknown rollout stage")
                .ctx("rollout_stage", static_cast<std::int64_t>(plan.rollout_stage))
        );
    }

}  // namespace timetable::domain::assignment
