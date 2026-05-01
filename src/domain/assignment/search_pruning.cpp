#include "timetable/domain/assignment/search_pruning.hpp"
#include "timetable/domain/assignment/search_pruning_plan.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

#include <mathfp/core/error.hpp>
#include <mathfp/core/try.hpp>
#include <mathfp/core/unit.hpp>
#include <mathfp/types/units.hpp>

namespace timetable::domain::assignment {
    namespace {

        void update_summary_with_metrics(
              SearchPruningSummary&     summary
            , const SearchPruningMetrics& metrics
        ) noexcept {
            if (summary.empty) {
                summary.min_impedance    = metrics.impedance;
                summary.min_journey_time = metrics.journey_time.value();
                summary.min_walk_time    = metrics.walk_time.value();
                summary.min_transfers    = static_cast<double>(metrics.transfers.get());
                summary.min_fare         = metrics.fare;
                summary.empty            = false;
                return;
            }

            summary.min_impedance    = std::min(summary.min_impedance   , metrics.impedance);
            summary.min_journey_time = std::min(summary.min_journey_time, metrics.journey_time.value());
            summary.min_walk_time    = std::min(summary.min_walk_time   , metrics.walk_time.value());
            summary.min_transfers    = std::min(summary.min_transfers   , static_cast<double>(metrics.transfers.get()));
            summary.min_fare         = std::min(summary.min_fare        , metrics.fare);
        }

    }  // namespace

    mathfp::Expected<SearchPruningMetrics> make_search_pruning_metrics(
        SearchPruningMetrics metrics
    ) {
        MATHFP_TRY(validate_search_pruning_metrics(metrics));
        return metrics;
    }

    mathfp::Expected<mathfp::Unit> validate_search_pruning_metrics(
        const SearchPruningMetrics& metrics
    ) {
        if (
            !(std::isfinite(metrics.departure.value()) && std::isfinite(metrics.arrival.value())
            && std::isfinite(metrics.journey_time.value()) && std::isfinite(metrics.walk_time.value())
            && std::isfinite(metrics.fare) && std::isfinite(metrics.impedance))
        ) {
            return mathfp::unexpected(
                mathfp::invalid_arg("search pruning metrics carry non-finite value")
            );
        }
        if (metrics.arrival.value() < metrics.departure.value()) {
            return mathfp::unexpected(
                mathfp::invalid_arg("search pruning metrics arrival precedes departure")
                    .ctx("departure", metrics.departure.value())
                    .ctx("arrival"  , metrics.arrival  .value())
            );
        }
        if (metrics.journey_time.value() < 0.0 || metrics.walk_time.value() < 0.0
            || metrics.fare < 0.0 || metrics.transfers.get() < 0) {
            return mathfp::unexpected(
                mathfp::invalid_arg("search pruning metrics contain negative value")
            );
        }
        if (metrics.journey_time.value()
            > metrics.arrival.value() - metrics.departure.value() + std::numeric_limits<double>::epsilon()) {
            return mathfp::unexpected(
                mathfp::invalid_arg("search pruning metrics journey_time exceeds arrival - departure")
            );
        }
        return mathfp::kUnit;
    }

    mathfp::Expected<mathfp::Unit> validate_search_pruning_summary(
        const SearchPruningSummary& summary
    ) {
        if (summary.empty) {
            return mathfp::kUnit;
        }
        if (
            !(std::isfinite(summary.min_impedance)
            && std::isfinite(summary.min_journey_time)
            && std::isfinite(summary.min_walk_time)
            && std::isfinite(summary.min_transfers)
            && std::isfinite(summary.min_fare))
        ) {
            return mathfp::unexpected(
                mathfp::invalid_arg("search pruning summary carries non-finite minimum")
            );
        }
        if (
               summary.min_journey_time < 0.0 || summary.min_walk_time < 0.0
            || summary.min_transfers    < 0.0 || summary.min_fare      < 0.0
        ) {
            return mathfp::unexpected(
                mathfp::invalid_arg("search pruning summary contains negative minimum")
            );
        }
        return mathfp::kUnit;
    }

    mathfp::Expected<mathfp::Unit> validate_search_pruning_metric_set(
        const SearchPruningMetricSet& metric_set
    ) {
        for (std::size_t i = 0; i < metric_set.metrics.size(); ++i) {
            MATHFP_TRY(validate_search_pruning_metrics(metric_set.metrics[i]));
            if (
                   i > 0
                && metric_set.metrics[i]    .arrival.value()
                 < metric_set.metrics[i - 1].arrival.value()
            ) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("search pruning metric set must be ordered by arrival")
                );
            }
        }
        MATHFP_TRY(validate_search_pruning_summary(metric_set.summary));

        for (std::size_t i = 0; i < metric_set.metrics.size(); ++i) {
            for (std::size_t j = 0; j < metric_set.metrics.size(); ++j) {
                if (i == j) {
                    continue;
                }
                if (dominates_exactly(metric_set.metrics[i], metric_set.metrics[j])) {
                    return mathfp::unexpected(
                        mathfp::invalid_arg("search pruning metric set contains exact-dominated metric vector")
                            .ctx("dominator_index", static_cast<std::int64_t>(i))
                            .ctx("dominated_index", static_cast<std::int64_t>(j))
                    );
                }
            }
        }

        const auto recomputed = summarize_pruning_metrics(metric_set.metrics);
        if (
            recomputed.empty != metric_set.summary.empty
            || (
                   !recomputed.empty
                && (recomputed.min_impedance    != metric_set.summary.min_impedance
                 || recomputed.min_journey_time != metric_set.summary.min_journey_time
                 || recomputed.min_walk_time    != metric_set.summary.min_walk_time
                 || recomputed.min_transfers    != metric_set.summary.min_transfers
                 || recomputed.min_fare         != metric_set.summary.min_fare)
            )
        ) {
            return mathfp::unexpected(
                mathfp::internal_error("search pruning metric set summary disagrees with metrics")
            );
        }

        return mathfp::kUnit;
    }

    bool dominates_exactly(
          ExactDominanceContract    contract
        , const SearchPruningMetrics& lhs
        , const SearchPruningMetrics& rhs
    ) noexcept {
        (void)contract;
        const auto no_worse =
               lhs.departure.value() >= rhs.departure.value()
            && lhs.arrival.value()   <= rhs.arrival.value()
            && lhs.impedance         <= rhs.impedance
            && lhs.transfers.get()   <= rhs.transfers.get();

        const auto strictly_better =
               lhs.departure.value() > rhs.departure.value()
            || lhs.arrival.value()   < rhs.arrival.value()
            || lhs.impedance         < rhs.impedance
            || lhs.transfers.get()   < rhs.transfers.get();

        return no_worse && strictly_better;
    }

    bool dominates_exactly(
          const SearchPruningMetrics& lhs
        , const SearchPruningMetrics& rhs
    ) noexcept {
        return dominates_exactly(ExactDominanceContract::ExtensionSafeCurrentState, lhs, rhs);
    }

    bool dominates_exactly(
          const ExactPruningPolicy& policy
        , const SearchPruningMetrics& lhs
        , const SearchPruningMetrics& rhs
    ) noexcept {
        return dominates_exactly(policy.contract, lhs, rhs);
    }

    bool is_exactly_relevant(
          const ExactPruningPolicy&           policy
        , const SearchPruningMetrics&           candidate
        , std::span<const SearchPruningMetrics> known
    ) noexcept {
        for (const auto& existing : known) {
            if (dominates_exactly(policy, existing, candidate)) {
                return false;
            }
        }
        return true;
    }

    bool is_exactly_relevant(
          const SearchPruningMetrics&           candidate
        , std::span<const SearchPruningMetrics> known
    ) noexcept {
        return is_exactly_relevant(ExactPruningPolicy{}, candidate, known);
    }

    SearchPruningSummary summarize_pruning_metrics(
        std::span<const SearchPruningMetrics> metrics
    ) noexcept {
        SearchPruningSummary summary{};
        for (const auto& metric : metrics) {
            update_summary_with_metrics(summary, metric);
        }
        return summary;
    }

    bool within_approximate_retention(
          const SearchPruningMetrics&       candidate
        , const SearchPruningSummary&     summary
        , const ApproximatePruningPolicy& policy
        , const TransferLimits&           limits
    ) noexcept {
        if (summary.empty) {
            return candidate.transfers <= limits.max_transfers;
        }

        return
               candidate.transfers <= limits.max_transfers
            && candidate.impedance
                   <= mathfp::units::as_dimless(policy.tolerances.imp_mult)
                    * summary.min_impedance
                    + mathfp::units::as_dimless(policy.tolerances.imp_add)
            && candidate.journey_time.value()
                   <= mathfp::units::as_dimless(policy.tolerances.jt_mult)
                    * summary.min_journey_time
                    + mathfp::units::as_dimless(policy.tolerances.jt_add)
            && static_cast<double>(candidate.transfers.get())
                   <= mathfp::units::as_dimless(policy.tolerances.nt_mult)
                    * summary.min_transfers
                    + mathfp::units::as_dimless(policy.tolerances.nt_add);
    }

    ExactPruningDecision evaluate_exact_pruning(
          const ExactPruningPolicy&    exact_policy
        , const SearchPruningMetrics&    candidate
        , const SearchPruningMetricSet& metric_set
    ) noexcept {
        if (!is_exactly_relevant(exact_policy, candidate, metric_set.metrics)) {
            return ExactPruningDecision{
                  .reason   = SearchPruningReason::RejectedExactDominance
                , .accepted = false
            };
        }

        return ExactPruningDecision{
              .reason   = SearchPruningReason::Accepted
            , .accepted = true
        };
    }

    ExactPruningDecision evaluate_exact_pruning(
          const SearchPruningMetrics&    candidate
        , const SearchPruningMetricSet& metric_set
    ) noexcept {
        return evaluate_exact_pruning(ExactPruningPolicy{}, candidate, metric_set);
    }

    ApproximatePruningDecision evaluate_approximate_pruning(
          const ApproximatePruningPolicy& approximate_policy
        , const SearchPruningMetrics&       candidate
        , const SearchPruningSummary&     summary
        , const TransferLimits&           limits
    ) noexcept {
        if (!within_approximate_retention(
              candidate
            , summary
            , approximate_policy
            , limits
        )) {
            return ApproximatePruningDecision{
                  .reason   = SearchPruningReason::RejectedApproximateTolerance
                , .accepted = false
            };
        }

        return ApproximatePruningDecision{
              .reason   = SearchPruningReason::Accepted
            , .accepted = true
        };
    }

    SearchPruningMetricSet insert_exact_pruning_metrics(
          const ExactPruningPolicy& exact_policy
        , SearchPruningMetricSet    metric_set
        , SearchPruningMetrics      metrics
    ) {
        const auto dominated_begin = std::lower_bound(
              metric_set.metrics.begin()
            , metric_set.metrics.end()
            , metrics.arrival.value()
            , [](const SearchPruningMetrics& lhs, double arrival_value) {
                return lhs.arrival.value() < arrival_value;
            }
        );

        const auto old_size = metric_set.metrics.size();
        metric_set.metrics.erase(
              std::remove_if(
                  dominated_begin
                , metric_set.metrics.end()
                , [&](const SearchPruningMetrics& existing) {
                    return dominates_exactly(exact_policy, metrics, existing);
                }
            )
          , metric_set.metrics.end()
        );

        const auto removed = old_size - metric_set.metrics.size();
        const auto insertion = std::lower_bound(
              metric_set.metrics.begin()
            , metric_set.metrics.end()
            , metrics.arrival.value()
            , [](const SearchPruningMetrics& lhs, double arrival_value) {
                return lhs.arrival.value() < arrival_value;
            }
        );
        const auto inserted_metrics = metrics;
        metric_set.metrics.insert(insertion, std::move(metrics));
        if (removed > 0) {
            metric_set.summary = summarize_pruning_metrics(metric_set.metrics);
        } else {
            update_summary_with_metrics(metric_set.summary, inserted_metrics);
        }
        return metric_set;
    }

    SearchPruningMetricSet insert_exact_pruning_metrics(
          SearchPruningMetricSet metric_set
        , SearchPruningMetrics   metrics
    ) {
        return insert_exact_pruning_metrics(ExactPruningPolicy{}, std::move(metric_set), std::move(metrics));
    }

    SearchPruningDecision evaluate_search_pruning(
          const ExactPruningPolicy&       exact_policy
        , const SearchPruningMetrics&       candidate
        , const SearchPruningMetricSet&    metric_set
        , const ApproximatePruningPolicy& approximate_policy
        , const TransferLimits&           limits
    ) noexcept {
        const auto exact_decision = evaluate_exact_pruning(
              exact_policy
            , candidate
            , metric_set
        );
        if (!exact_decision.accepted) {
            return SearchPruningDecision{
                  .layer    = SearchPruningLayer::Exact
                , .reason   = exact_decision.reason
                , .accepted = false
            };
        }

        const auto approximate_decision = evaluate_approximate_pruning(
              approximate_policy
            , candidate
            , metric_set.summary
            , limits
        );
        if (!approximate_decision.accepted) {
            return SearchPruningDecision{
                  .layer    = SearchPruningLayer::Approximate
                , .reason   = approximate_decision.reason
                , .accepted = false
            };
        }

        return SearchPruningDecision{
              .layer    = SearchPruningLayer::Approximate
            , .reason   = SearchPruningReason::Accepted
            , .accepted = true
        };
    }

    SearchPruningDecision evaluate_search_pruning(
          const SearchPruningMetrics&        candidate
        , const SearchPruningMetricSet&     metric_set
        , const ApproximatePruningPolicy&  approximate_policy
        , const TransferLimits&            limits
    ) noexcept {
        return evaluate_search_pruning(
              ExactPruningPolicy{}
            , candidate
            , metric_set
            , approximate_policy
            , limits
        );
    }

    bool stores_search_pruning_metrics(
        const SearchPruningExecutionPlan& execution
    ) noexcept {
        return execution.exact_enabled;
    }

    SearchPruningDecision evaluate_search_pruning(
          const SearchPruningExecutionPlan& execution
        , const SearchPruningMetrics&         candidate
        , const SearchPruningMetricSet&      metric_set
        , const TransferLimits&             limits
    ) noexcept {
        if (!execution.exact_enabled && !execution.approximate_enabled) {
            return SearchPruningDecision{
                  .layer    = SearchPruningLayer::Exact
                , .reason   = SearchPruningReason::Accepted
                , .accepted = true
            };
        }

        const auto exact_decision =
            execution.exact_enabled
                ? evaluate_exact_pruning(execution.exact_policy, candidate, metric_set)
                : ExactPruningDecision{};
        if (!exact_decision.accepted) {
            return SearchPruningDecision{
                  .layer    = SearchPruningLayer::Exact
                , .reason   = exact_decision.reason
                , .accepted = false
            };
        }

        const auto approximate_decision =
            execution.approximate_enabled && execution.approximate_policy.has_value()
                ? evaluate_approximate_pruning(
                      *execution.approximate_policy
                    , candidate
                    , metric_set.summary
                    , limits
                )
                : ApproximatePruningDecision{};
        if (!approximate_decision.accepted) {
            return SearchPruningDecision{
                  .layer    = SearchPruningLayer::Approximate
                , .reason   = approximate_decision.reason
                , .accepted = false
            };
        }

        return SearchPruningDecision{
              .layer    = execution.approximate_enabled
                    ? SearchPruningLayer::Approximate
                    : SearchPruningLayer::Exact
            , .reason   = SearchPruningReason::Accepted
            , .accepted = true
        };
    }

    SearchPruningMetricSet insert_search_pruning_metrics(
          const SearchPruningExecutionPlan& execution
        , SearchPruningMetricSet             metric_set
        , SearchPruningMetrics               metrics
    ) {
        if (!stores_search_pruning_metrics(execution)) {
            return metric_set;
        }
        return insert_exact_pruning_metrics(execution.exact_policy, std::move(metric_set), std::move(metrics));
    }

}  // namespace timetable::domain::assignment
