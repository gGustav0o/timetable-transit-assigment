#include "timetable/domain/assignment/search/pruning/suffix_lower_bound.hpp"

#include <algorithm>
#include <limits>

#include <mathfp/core/try.hpp>
#include <mathfp/types/units.hpp>

#include "timetable/domain/assignment/search/relations/branch_metrics.hpp"
#include "timetable/domain/assignment/search/relations/branch_state_projection.hpp"
#include "timetable/domain/impedance.hpp"

namespace timetable::domain::assignment {
    namespace {

        [[nodiscard]] CompleteConnectionMetricSummary summarize_complete_metrics(
            const CompleteConnectionRetention& retention
        ) noexcept {
            CompleteConnectionMetricSummary summary{
                  .min_impedance    = std::numeric_limits<double>::infinity()
                , .min_journey_time = std::numeric_limits<double>::infinity()
                , .min_transfers    = std::numeric_limits<double>::infinity()
                , .empty            = retention.alternatives.empty()
            };
            for (const auto& alternative : retention.alternatives) {
                summary.min_impedance = std::min(
                      summary.min_impedance
                    , alternative.metrics.impedance
                );
                summary.min_journey_time = std::min(
                      summary.min_journey_time
                    , alternative.metrics.journey_time.value()
                );
                summary.min_transfers = std::min(
                      summary.min_transfers
                    , static_cast<double>(alternative.metrics.transfers.get())
                );
            }
            return summary;
        }

        [[nodiscard]] CompleteConnectionMetricSummary summarize_complete_metrics(
            const CompactCompleteConnectionRetention& retention
        ) noexcept {
            CompleteConnectionMetricSummary summary{
                  .min_impedance    = std::numeric_limits<double>::infinity()
                , .min_journey_time = std::numeric_limits<double>::infinity()
                , .min_transfers    = std::numeric_limits<double>::infinity()
                , .empty            = retention.metrics.empty()
            };
            for (const auto& metrics : retention.metrics) {
                summary.min_impedance = std::min(
                      summary.min_impedance
                    , metrics.impedance
                );
                summary.min_journey_time = std::min(
                      summary.min_journey_time
                    , metrics.journey_time.value()
                );
                summary.min_transfers = std::min(
                      summary.min_transfers
                    , static_cast<double>(metrics.transfers.get())
                );
            }
            return summary;
        }

        [[nodiscard]] mathfp::Expected<double> partial_impedance_value(
              const SearchBranch&      branch
            , const SearchCostContext& search_cost
        ) {
            const auto cost_components = SearchCostComponents{
                  .base = partial_impedance_components(branch.metrics)
                , .capacity_exposure = branch.metrics.capacity_exposure
            };
            return search_impedance(
                  cost_components
                , search_cost
            );
        }

        [[nodiscard]] Time partial_journey_time_lower_bound(
            const SearchBranch& branch
        ) noexcept {
            if (branch.metrics.departure.has_value()) {
                return partial_journey_time(branch.metrics);
            }
            return branch.metrics.access_time;
        }

        [[nodiscard]] double suffix_capacity_impedance_lower_bound(
            const SearchCostContext& search_cost
        ) noexcept {
            switch (search_cost.mode) {
                case SearchCostMode::BaseOnly:
                    return 0.0;

                case SearchCostMode::CapacityAware:
                    return 0.0;
            }

            return 0.0;
        }

    }  // namespace

    mathfp::Expected<CompletionMetricLowerBound> completion_metric_lower_bound(
          const SearchBranch&              branch
        , const ResidualSuffixLowerBounds& suffix
        , const SearchCostContext&         search_cost
    ) {
        MATHFP_TRY_LET(
              double
            , partial_impedance
            , partial_impedance_value(branch, search_cost)
        );
        const auto suffix_capacity_impedance =
            suffix_capacity_impedance_lower_bound(search_cost);
        return CompletionMetricLowerBound{
              .departure   = branch.metrics.departure
            , .arrival     = branch.metrics.current_time.has_value()
                ? std::optional<Time>{
                    Time{ branch.metrics.current_time->value() + suffix.journey_time.value() }
                }
                : std::nullopt
            , .journey_time = Time{
                  partial_journey_time_lower_bound(branch).value()
                + suffix.journey_time.value()
              }
            , .transfers    = static_cast<double>(branch.metrics.transfers.get())
                + static_cast<double>(suffix.transfers.get())
            , .impedance    = partial_impedance
                + suffix.impedance
                + suffix_capacity_impedance
        };
    }

    bool complete_connection_dominates_completion_lower_bound(
          const CompleteConnectionDominanceConfig& dominance_config
        , const CompleteConnectionMetrics&         complete
        , const CompletionMetricLowerBound&        lower_bound
    ) noexcept {
        if (!complete_connection_can_dominate(dominance_config, complete)) {
            return false;
        }
        if (!lower_bound.departure.has_value() || !lower_bound.arrival.has_value()) {
            return false;
        }

        const auto no_worse =
               complete.departure.value() >= lower_bound.departure->value()
            && complete.arrival.value()   <= lower_bound.arrival->value()
            && complete.impedance         <= lower_bound.impedance
            && static_cast<double>(complete.transfers.get()) <= lower_bound.transfers;

        const auto strictly_better =
               complete.departure.value() > lower_bound.departure->value()
            || complete.arrival.value()   < lower_bound.arrival->value()
            || complete.impedance         < lower_bound.impedance
            || static_cast<double>(complete.transfers.get()) < lower_bound.transfers;

        return no_worse && strictly_better;
    }

    bool violates_complete_tolerance_lower_bound(
          const CompletionMetricLowerBound&      lower_bound
        , const CompleteConnectionMetricSummary& summary
        , const ChoiceTolerances&                tolerances
        , SuffixLowerBoundRejectionReason&       reason
    ) noexcept {
        if (summary.empty) {
            return false;
        }

        const auto impedance_bound =
              tolerances.imp_mult.value()
            * summary.min_impedance
            + tolerances.imp_add.value();
        if (lower_bound.impedance > impedance_bound) {
            reason = SuffixLowerBoundRejectionReason::ToleranceImpedance;
            return true;
        }

        const auto journey_time_bound =
              tolerances.jt_mult.value()
            * summary.min_journey_time
            + tolerances.jt_add.seconds();
        if (lower_bound.journey_time.value() > journey_time_bound) {
            reason = SuffixLowerBoundRejectionReason::ToleranceJourneyTime;
            return true;
        }

        const auto transfer_bound =
              tolerances.nt_mult.value()
            * summary.min_transfers
            + tolerances.nt_add.value();
        if (lower_bound.transfers > transfer_bound) {
            reason = SuffixLowerBoundRejectionReason::ToleranceTransfers;
            return true;
        }

        return false;
    }

    mathfp::Expected<SuffixLowerBoundPruningDecision>
    evaluate_suffix_lower_bound_pruning(
          const SearchBranch&                branch
        , ZoneId                             destination
        , const ResidualReachability&        reachability
        , const CompleteConnectionRetention& complete_retention
        , const SearchParams&                params
        , const SearchCostContext&           search_cost
        , const ChoiceConfig&                choice_config
        , const CompleteConnectionDominanceConfig& dominance_config
    ) {
        if (complete_retention.alternatives.empty()) {
            return SuffixLowerBoundPruningDecision{ .feasible = true };
        }

        const auto destination_it = reachability.destinations.find(destination);
        if (destination_it == reachability.destinations.end()) {
            return SuffixLowerBoundPruningDecision{ .feasible = true };
        }

        const auto state = residual_reachability_key(
            relaxed_suffix_state(branch, destination, params.transfers)
        );
        const auto lower_bound_it = destination_it->second.suffix_lower_bounds.find(state);
        if (lower_bound_it == destination_it->second.suffix_lower_bounds.end()) {
            return SuffixLowerBoundPruningDecision{ .feasible = true };
        }

        MATHFP_TRY_LET(
              CompletionMetricLowerBound
            , lower_bound
            , completion_metric_lower_bound(
                  branch
                , lower_bound_it->second
                , search_cost
            )
        );

        for (const auto& complete : complete_retention.alternatives) {
            if (complete_connection_dominates_completion_lower_bound(
                  dominance_config
                , complete.metrics
                , lower_bound
            )) {
                return SuffixLowerBoundPruningDecision{
                      .feasible = false
                    , .rejection_reason = SuffixLowerBoundRejectionReason::ExactDominance
                };
            }
        }

        if (choice_config.rollout_stage != ChoiceRolloutStage::ExactAndApproximate) {
            return SuffixLowerBoundPruningDecision{ .feasible = true };
        }

        auto reason = SuffixLowerBoundRejectionReason::ToleranceImpedance;
        if (violates_complete_tolerance_lower_bound(
              lower_bound
            , summarize_complete_metrics(complete_retention)
            , params.choice_tolerances
            , reason
        )) {
            return SuffixLowerBoundPruningDecision{
                  .feasible = false
                , .rejection_reason = reason
            };
        }

        return SuffixLowerBoundPruningDecision{ .feasible = true };
    }

    mathfp::Expected<SuffixLowerBoundPruningDecision>
    evaluate_suffix_lower_bound_pruning(
          const SearchBranch&                       branch
        , ZoneId                                    destination
        , const ResidualReachability&               reachability
        , const CompactCompleteConnectionRetention& complete_retention
        , const SearchParams&                       params
        , const SearchCostContext&                  search_cost
        , const ChoiceConfig&                       choice_config
        , const CompleteConnectionDominanceConfig&  dominance_config
    ) {
        if (complete_retention.metrics.empty()) {
            return SuffixLowerBoundPruningDecision{ .feasible = true };
        }

        const auto destination_it = reachability.destinations.find(destination);
        if (destination_it == reachability.destinations.end()) {
            return SuffixLowerBoundPruningDecision{ .feasible = true };
        }

        const auto state = residual_reachability_key(
            relaxed_suffix_state(branch, destination, params.transfers)
        );
        const auto lower_bound_it = destination_it->second.suffix_lower_bounds.find(state);
        if (lower_bound_it == destination_it->second.suffix_lower_bounds.end()) {
            return SuffixLowerBoundPruningDecision{ .feasible = true };
        }

        MATHFP_TRY_LET(
              CompletionMetricLowerBound
            , lower_bound
            , completion_metric_lower_bound(
                  branch
                , lower_bound_it->second
                , search_cost
            )
        );

        for (const auto& complete : complete_retention.metrics) {
            if (complete_connection_dominates_completion_lower_bound(
                  dominance_config
                , complete
                , lower_bound
            )) {
                return SuffixLowerBoundPruningDecision{
                      .feasible = false
                    , .rejection_reason = SuffixLowerBoundRejectionReason::ExactDominance
                };
            }
        }

        if (choice_config.rollout_stage != ChoiceRolloutStage::ExactAndApproximate) {
            return SuffixLowerBoundPruningDecision{ .feasible = true };
        }

        auto reason = SuffixLowerBoundRejectionReason::ToleranceImpedance;
        if (violates_complete_tolerance_lower_bound(
              lower_bound
            , summarize_complete_metrics(complete_retention)
            , params.choice_tolerances
            , reason
        )) {
            return SuffixLowerBoundPruningDecision{
                  .feasible = false
                , .rejection_reason = reason
            };
        }

        return SuffixLowerBoundPruningDecision{ .feasible = true };
    }

}  // namespace timetable::domain::assignment
