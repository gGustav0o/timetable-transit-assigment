#pragma once

#include <cstdint>
#include <optional>

#include <mathfp/core/expected.hpp>

#include "timetable/domain/assignment/choice/choice_config.hpp"
#include "timetable/domain/assignment/complete_connection_retention.hpp"
#include "timetable/domain/assignment/search/model/branch.hpp"
#include "timetable/domain/assignment/search/projection.hpp"
#include "timetable/domain/assignment/search/residual_reachability.hpp"
#include "timetable/domain/assignment/search_cost.hpp"
#include "timetable/domain/params.hpp"

namespace timetable::domain::assignment {

    enum class SuffixLowerBoundRejectionReason : std::uint8_t {
          ExactDominance
        , ToleranceImpedance
        , ToleranceJourneyTime
        , ToleranceTransfers
    };

    struct CompletionMetricLowerBound final {
        std::optional<Time> departure{};
        std::optional<Time> arrival{};
        Time                journey_time{};
        double              transfers{};
        double              impedance{};
    };

    struct SuffixLowerBoundPruningDecision final {
        bool                            feasible{ true };
        SuffixLowerBoundRejectionReason rejection_reason{
            SuffixLowerBoundRejectionReason::ToleranceImpedance
        };
    };

    [[nodiscard]] mathfp::Expected<CompletionMetricLowerBound> completion_metric_lower_bound(
          const SearchBranch&               branch
        , const ResidualSuffixLowerBounds&  suffix
        , const SearchCostContext&          search_cost
    );

    [[nodiscard]] bool complete_connection_dominates_completion_lower_bound(
          const CompleteConnectionDominanceConfig& dominance_config
        , const CompleteConnectionMetrics&         complete
        , const CompletionMetricLowerBound&        lower_bound
    ) noexcept;

    [[nodiscard]] bool violates_complete_tolerance_lower_bound(
          const CompletionMetricLowerBound&      lower_bound
        , const CompleteConnectionMetricSummary& summary
        , const ChoiceTolerances&                tolerances
        , SuffixLowerBoundRejectionReason&       reason
    ) noexcept;

    [[nodiscard]] mathfp::Expected<SuffixLowerBoundPruningDecision>
    evaluate_suffix_lower_bound_pruning(
          const SearchBranch&                branch
        , ZoneId                             destination
        , const ResidualReachability&        reachability
        , const CompleteConnectionRetention& complete_retention
        , const SearchParams&                params
        , const SearchCostContext&           search_cost
        , const ChoiceConfig&                choice_config
        , const CompleteConnectionDominanceConfig& dominance_config
    );

    [[nodiscard]] mathfp::Expected<SuffixLowerBoundPruningDecision>
    evaluate_suffix_lower_bound_pruning(
          const SearchBranch&                       branch
        , ZoneId                                    destination
        , const ResidualReachability&               reachability
        , const CompactCompleteConnectionRetention& complete_retention
        , const SearchParams&                       params
        , const SearchCostContext&                  search_cost
        , const ChoiceConfig&                       choice_config
        , const CompleteConnectionDominanceConfig&  dominance_config
    );

}  // namespace timetable::domain::assignment
