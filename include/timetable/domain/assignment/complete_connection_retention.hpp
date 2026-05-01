#pragma once

#include <cstddef>
#include <vector>

#include <mathfp/core/expected.hpp>

#include "timetable/domain/assignment/choice/choice_config.hpp"
#include "timetable/domain/assignment/search_cost.hpp"
#include "timetable/domain/assignment/search/search.hpp"
#include "timetable/domain/params.hpp"

namespace timetable::domain::assignment {

    /**
     * @brief Domain policy for dominance between complete OD connections.
     *
     * searchPara.deactivateDominanceOfDirectConnections belongs to this layer:
     * it controls dominance between complete alternatives retained for one
     * OD-time task. It is intentionally separate from partial search pruning
     * and from the final choice model.
     *
     * Direct connection is defined mathematically as a complete connection with
     * zero transfers. When the flag is enabled, a direct complete connection is
     * not allowed to dominate another complete connection.
     */
    struct CompleteConnectionDominanceConfig final {
        bool deactivate_dominance_of_direct_connections{ false };
    };

    struct CompleteConnectionMetrics final {
        Time          departure{};
        Time          arrival{};
        Time          journey_time{};
        TransferCount transfers{};
        double        impedance{};
    };

    struct CompleteConnectionAlternative final {
        SearchConnection          connection;
        CompleteConnectionMetrics metrics{};
    };

    struct CompleteConnectionMetricSummary final {
        double min_impedance{};
        double min_journey_time{};
        double min_transfers{};
        bool   empty{ true };
    };

    struct CompleteConnectionRetentionDecision final {
        bool        accepted{};
        std::size_t removed_dominated{};
    };

    /**
     * @brief Task-local canonical retention container for complete connections.
     *
     * Invariant after each successful insertion:
     * - alternatives are exact-nondominated under complete-connection dominance;
     * - no tolerance filtering has been applied yet, because final tolerance
     *   bounds depend on task-final minima.
     */
    struct CompleteConnectionRetention final {
        std::vector<CompleteConnectionAlternative> alternatives{};
    };

    [[nodiscard]] mathfp::Expected<CompleteConnectionMetrics> complete_connection_metrics(
          const SearchConnection& connection
        , const SearchCostContext& search_cost
        , IntervalId              interval
    );

    [[nodiscard]] bool is_direct_connection(
        const CompleteConnectionMetrics& metrics
    ) noexcept;

    [[nodiscard]] bool complete_connection_can_dominate(
          const CompleteConnectionDominanceConfig& config
        , const CompleteConnectionMetrics&         metrics
    ) noexcept;

    [[nodiscard]] bool complete_connection_dominates(
          const CompleteConnectionMetrics& lhs
        , const CompleteConnectionMetrics& rhs
    ) noexcept;

    [[nodiscard]] bool complete_connection_dominates(
          const CompleteConnectionDominanceConfig& config
        , const CompleteConnectionMetrics&         lhs
        , const CompleteConnectionMetrics&         rhs
    ) noexcept;

    [[nodiscard]] bool within_complete_connection_tolerances(
          const CompleteConnectionMetrics&       metrics
        , const CompleteConnectionMetricSummary& summary
        , const ChoiceTolerances&                tolerances
    ) noexcept;

    [[nodiscard]] mathfp::Expected<CompleteConnectionRetentionDecision> retain_exact_complete_connection(
          CompleteConnectionRetention& retention
        , SearchConnection             connection
        , const SearchCostContext&     search_cost
        , IntervalId                   interval
    );

    [[nodiscard]] mathfp::Expected<CompleteConnectionRetentionDecision> retain_exact_complete_connection(
          CompleteConnectionRetention&            retention
        , SearchConnection                        connection
        , const SearchCostContext&                search_cost
        , IntervalId                              interval
        , const CompleteConnectionDominanceConfig& dominance_config
    );

    [[nodiscard]] std::vector<SearchConnection> finalize_complete_connection_retention(
          const CompleteConnectionRetention& retention
        , const ChoiceTolerances&            tolerances
        , ChoiceRolloutStage                 rollout_stage
    );

    [[nodiscard]] mathfp::Expected<std::vector<SearchConnection>> refine_complete_connection_ptrs(
          const std::vector<const SearchConnection*>& connections
        , const SearchCostContext&                    search_cost
        , IntervalId                                  interval
        , const ChoiceTolerances&                     tolerances
        , ChoiceRolloutStage                          rollout_stage
    );

}  // namespace timetable::domain::assignment
