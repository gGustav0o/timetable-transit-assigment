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

        void update_summary_with_label(
              SearchPruningSummary&     summary
            , const SearchPruningLabel& label
        ) noexcept {
            const auto& primitive = label.primitive;
            const auto& derived   = label.derived;
            if (summary.empty) {
                summary.min_impedance    = derived.impedance;
                summary.min_journey_time = derived.journey_time.value();
                summary.min_walk_time    = derived.walk_time.value();
                summary.min_transfers    = static_cast<double>(primitive.transfers.get());
                summary.min_fare         = primitive.fare;
                summary.empty            = false;
                return;
            }

            summary.min_impedance    = std::min(summary.min_impedance   , derived.impedance);
            summary.min_journey_time = std::min(summary.min_journey_time, derived.journey_time.value());
            summary.min_walk_time    = std::min(summary.min_walk_time   , derived.walk_time.value());
            summary.min_transfers    = std::min(summary.min_transfers   , static_cast<double>(primitive.transfers.get()));
            summary.min_fare         = std::min(summary.min_fare        , primitive.fare);
        }

    }  // namespace

    mathfp::Expected<SearchPruningLabel> make_search_pruning_label(
          SearchPruningLabelPrimitive primitive
        , SearchPruningLabelDerived   derived
    ) {
        SearchPruningLabel label{
              .primitive = std::move(primitive)
            , .derived   = std::move(derived)
        };
        MATHFP_TRY(validate_search_pruning_label(label));
        return label;
    }

    mathfp::Expected<mathfp::Unit> validate_search_pruning_label(
        const SearchPruningLabel& label
    ) {
        const auto& primitive = label.primitive;
        const auto& derived   = label.derived;
        if (
            !(std::isfinite(primitive.departure.value()) && std::isfinite(primitive.arrival.value())
            && std::isfinite(derived.journey_time.value()) && std::isfinite(derived.walk_time.value())
            && std::isfinite(primitive.fare) && std::isfinite(derived.impedance))
        ) {
            return mathfp::unexpected(
                mathfp::invalid_arg("search pruning label carries non-finite metric")
            );
        }
        if (primitive.arrival.value() < primitive.departure.value()) {
            return mathfp::unexpected(
                mathfp::invalid_arg("search pruning label arrival precedes departure")
                    .ctx("departure", primitive.departure.value())
                    .ctx("arrival"  , primitive.arrival  .value())
            );
        }
        if (derived.journey_time.value() < 0.0 || derived.walk_time.value() < 0.0
            || primitive.fare < 0.0 || primitive.transfers.get() < 0) {
            return mathfp::unexpected(
                mathfp::invalid_arg("search pruning label contains negative metric")
            );
        }
        if (derived.journey_time.value()
            > primitive.arrival.value() - primitive.departure.value() + std::numeric_limits<double>::epsilon()) {
            return mathfp::unexpected(
                mathfp::invalid_arg("search pruning label journey_time exceeds arrival - departure")
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

    mathfp::Expected<mathfp::Unit> validate_search_pruning_label_set(
        const SearchPruningLabelSet& label_set
    ) {
        for (std::size_t i = 0; i < label_set.labels.size(); ++i) {
            MATHFP_TRY(validate_search_pruning_label(label_set.labels[i]));
            if (
                   i > 0
                && label_set.labels[i]    .primitive.arrival.value()
                 < label_set.labels[i - 1].primitive.arrival.value()
            ) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("search pruning label set must be ordered by arrival")
                );
            }
        }
        MATHFP_TRY(validate_search_pruning_summary(label_set.summary));

        for (std::size_t i = 0; i < label_set.labels.size(); ++i) {
            for (std::size_t j = 0; j < label_set.labels.size(); ++j) {
                if (i == j) {
                    continue;
                }
                if (dominates_exactly(label_set.labels[i], label_set.labels[j])) {
                    return mathfp::unexpected(
                        mathfp::invalid_arg("search pruning label set contains exact-dominated label")
                            .ctx("dominator_index", static_cast<std::int64_t>(i))
                            .ctx("dominated_index", static_cast<std::int64_t>(j))
                    );
                }
            }
        }

        const auto recomputed = summarize_pruning_labels(label_set.labels);
        if (
            recomputed.empty != label_set.summary.empty
            || (
                   !recomputed.empty
                && (recomputed.min_impedance    != label_set.summary.min_impedance
                 || recomputed.min_journey_time != label_set.summary.min_journey_time
                 || recomputed.min_walk_time    != label_set.summary.min_walk_time
                 || recomputed.min_transfers    != label_set.summary.min_transfers
                 || recomputed.min_fare         != label_set.summary.min_fare)
            )
        ) {
            return mathfp::unexpected(
                mathfp::internal_error("search pruning label set summary disagrees with labels")
            );
        }

        return mathfp::kUnit;
    }

    bool dominates_exactly(
          ExactDominanceContract    contract
        , const SearchPruningLabel& lhs
        , const SearchPruningLabel& rhs
    ) noexcept {
        (void)contract;
        const auto& lp = lhs.primitive;
        const auto& ld = lhs.derived;
        const auto& rp = rhs.primitive;
        const auto& rd = rhs.derived;

        const auto no_worse =
               lp.departure.value() >= rp.departure.value()
            && lp.arrival.value()   <= rp.arrival.value()
            && ld.impedance         <= rd.impedance
            && lp.transfers.get()   <= rp.transfers.get();

        const auto strictly_better =
               lp.departure.value() > rp.departure.value()
            || lp.arrival.value()   < rp.arrival.value()
            || ld.impedance         < rd.impedance
            || lp.transfers.get()   < rp.transfers.get();

        return no_worse && strictly_better;
    }

    bool dominates_exactly(
          const SearchPruningLabel& lhs
        , const SearchPruningLabel& rhs
    ) noexcept {
        return dominates_exactly(ExactDominanceContract::ExtensionSafeCurrentState, lhs, rhs);
    }

    bool dominates_exactly(
          const ExactPruningPolicy& policy
        , const SearchPruningLabel& lhs
        , const SearchPruningLabel& rhs
    ) noexcept {
        return dominates_exactly(policy.contract, lhs, rhs);
    }

    bool is_exactly_relevant(
          const ExactPruningPolicy&           policy
        , const SearchPruningLabel&           candidate
        , std::span<const SearchPruningLabel> known
    ) noexcept {
        for (const auto& existing : known) {
            if (dominates_exactly(policy, existing, candidate)) {
                return false;
            }
        }
        return true;
    }

    bool is_exactly_relevant(
          const SearchPruningLabel&           candidate
        , std::span<const SearchPruningLabel> known
    ) noexcept {
        return is_exactly_relevant(ExactPruningPolicy{}, candidate, known);
    }

    SearchPruningSummary summarize_pruning_labels(
        std::span<const SearchPruningLabel> labels
    ) noexcept {
        SearchPruningSummary summary{};
        for (const auto& label : labels) {
            update_summary_with_label(summary, label);
        }
        return summary;
    }

    bool within_approximate_retention(
          const SearchPruningLabel&       candidate
        , const SearchPruningSummary&     summary
        , const ApproximatePruningPolicy& policy
        , const TransferLimits&           limits
    ) noexcept {
        if (summary.empty) {
            return candidate.primitive.transfers <= limits.max_transfers;
        }

        return
               candidate.primitive.transfers <= limits.max_transfers
            && candidate.derived.impedance
                   <= mathfp::units::as_dimless(policy.tolerances.imp_mult)
                    * summary.min_impedance
                    + mathfp::units::as_dimless(policy.tolerances.imp_add)
            && candidate.derived.journey_time.value()
                   <= mathfp::units::as_dimless(policy.tolerances.jt_mult)
                    * summary.min_journey_time
                    + mathfp::units::as_dimless(policy.tolerances.jt_add)
            && static_cast<double>(candidate.primitive.transfers.get())
                   <= mathfp::units::as_dimless(policy.tolerances.nt_mult)
                    * summary.min_transfers
                    + mathfp::units::as_dimless(policy.tolerances.nt_add);
    }

    ExactPruningDecision evaluate_exact_pruning(
          const ExactPruningPolicy&    exact_policy
        , const SearchPruningLabel&    candidate
        , const SearchPruningLabelSet& label_set
    ) noexcept {
        if (!is_exactly_relevant(exact_policy, candidate, label_set.labels)) {
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
          const SearchPruningLabel&    candidate
        , const SearchPruningLabelSet& label_set
    ) noexcept {
        return evaluate_exact_pruning(ExactPruningPolicy{}, candidate, label_set);
    }

    ApproximatePruningDecision evaluate_approximate_pruning(
          const ApproximatePruningPolicy& approximate_policy
        , const SearchPruningLabel&       candidate
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

    SearchPruningLabelSet insert_exact_pruning_label(
          const ExactPruningPolicy& exact_policy
        , SearchPruningLabelSet     label_set
        , SearchPruningLabel        label
    ) {
        label_set.labels.erase(
            std::remove_if(
                  label_set.labels.begin()
                , label_set.labels.end()
                , [&](const SearchPruningLabel& existing) {
                    return dominates_exactly(exact_policy, label, existing);
                }
            )
          , label_set.labels.end()
        );

        const auto insertion = std::lower_bound(
              label_set.labels.begin()
            , label_set.labels.end()
            , label.primitive.arrival.value()
            , [](const SearchPruningLabel& lhs, double arrival_value) {
                return lhs.primitive.arrival.value() < arrival_value;
            }
        );
        label_set.labels.insert(insertion, std::move(label));
        label_set.summary = summarize_pruning_labels(label_set.labels);
        return label_set;
    }

    SearchPruningLabelSet insert_exact_pruning_label(
          SearchPruningLabelSet label_set
        , SearchPruningLabel    label
    ) {
        return insert_exact_pruning_label(ExactPruningPolicy{}, std::move(label_set), std::move(label));
    }

    SearchPruningDecision evaluate_search_pruning(
          const ExactPruningPolicy&       exact_policy
        , const SearchPruningLabel&       candidate
        , const SearchPruningLabelSet&    label_set
        , const ApproximatePruningPolicy& approximate_policy
        , const TransferLimits&           limits
    ) noexcept {
        const auto exact_decision = evaluate_exact_pruning(
              exact_policy
            , candidate
            , label_set
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
            , label_set.summary
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
          const SearchPruningLabel&        candidate
        , const SearchPruningLabelSet&     label_set
        , const ApproximatePruningPolicy&  approximate_policy
        , const TransferLimits&            limits
    ) noexcept {
        return evaluate_search_pruning(
              ExactPruningPolicy{}
            , candidate
            , label_set
            , approximate_policy
            , limits
        );
    }

    bool stores_search_pruning_labels(
        const SearchPruningExecutionPlan& execution
    ) noexcept {
        return execution.exact_enabled;
    }

    SearchPruningDecision evaluate_search_pruning(
          const SearchPruningExecutionPlan& execution
        , const SearchPruningLabel&         candidate
        , const SearchPruningLabelSet&      label_set
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
                ? evaluate_exact_pruning(execution.exact_policy, candidate, label_set)
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
                    , label_set.summary
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

    SearchPruningLabelSet insert_search_pruning_label(
          const SearchPruningExecutionPlan& execution
        , SearchPruningLabelSet             label_set
        , SearchPruningLabel                label
    ) {
        if (!stores_search_pruning_labels(execution)) {
            return label_set;
        }
        return insert_exact_pruning_label(execution.exact_policy, std::move(label_set), std::move(label));
    }

}  // namespace timetable::domain::assignment
