#pragma once

#include <cstddef>
#include <vector>

#include "timetable/domain/assignment/choice/choice_config.hpp"
#include "timetable/domain/assignment/search/search.hpp"
#include "timetable/domain/params.hpp"

namespace timetable::domain::assignment {

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

    [[nodiscard]] CompleteConnectionMetrics complete_connection_metrics(
          const SearchConnection& connection
        , const SearchParams&     params
        , double                  fare_scale
    );

    [[nodiscard]] bool complete_connection_dominates(
          const CompleteConnectionMetrics& lhs
        , const CompleteConnectionMetrics& rhs
    ) noexcept;

    [[nodiscard]] bool within_complete_connection_tolerances(
          const CompleteConnectionMetrics&       metrics
        , const CompleteConnectionMetricSummary& summary
        , const ChoiceTolerances&                tolerances
    ) noexcept;

    [[nodiscard]] CompleteConnectionRetentionDecision retain_exact_complete_connection(
          CompleteConnectionRetention& retention
        , SearchConnection             connection
        , const SearchParams&          params
        , double                       fare_scale
    );

    [[nodiscard]] std::vector<SearchConnection> finalize_complete_connection_retention(
          const CompleteConnectionRetention& retention
        , const ChoiceTolerances&            tolerances
        , ChoiceRolloutStage                 rollout_stage
    );

    [[nodiscard]] std::vector<SearchConnection> refine_complete_connection_ptrs(
          const std::vector<const SearchConnection*>& connections
        , const SearchParams&                         params
        , double                                      fare_scale
        , ChoiceRolloutStage                          rollout_stage
    );

}  // namespace timetable::domain::assignment
