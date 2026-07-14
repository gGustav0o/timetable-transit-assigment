#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include <mathfp/core/expected.hpp>

#include "timetable/domain/assignment/complete_connection_retention.hpp"
#include "timetable/domain/assignment/day_path.hpp"
#include "timetable/domain/assignment/search/model/retention.hpp"
#include "timetable/domain/assignment/search/search.hpp"
#include "timetable/domain/assignment/search_time_domain.hpp"
#include "timetable/domain/model.hpp"

namespace timetable::domain::assignment {

    enum class SearchProjectionSlotKind : std::uint8_t {
          DemandTask
        , OdDayPair
        , CompletionTarget
    };

    [[nodiscard]] constexpr const char* to_log_token(
        SearchProjectionSlotKind kind
    ) noexcept {
        switch (kind) {
            case SearchProjectionSlotKind::DemandTask:
                return "demand_task";
            case SearchProjectionSlotKind::OdDayPair:
                return "od_day_pair";
            case SearchProjectionSlotKind::CompletionTarget:
                return "completion_target";
        }
        return "unknown";
    }

    struct SearchProjectionSlot final {
        SearchProjectionSlotKind                 kind{ SearchProjectionSlotKind::DemandTask };
        ZoneId                                   origin;
        ZoneId                                   destination;
        std::optional<IntervalId>                interval{};
        std::optional<SearchTaskRef>             task_ref{};
        std::optional<std::reference_wrapper<const SearchTask>> task{};
        std::optional<std::size_t>               result_index{};
        std::optional<SearchCompletionTargetRef> completion_target{};
    };

    /*
     * TODO: Continue replacing long-lived nullable borrowed pointers in
     * search-facing domain contracts with references, optional references, or
     * small lookup/value-id types where that expresses the invariant more
     * precisely. Short local find/get_if pointers and C API boundaries can stay
     * as pointers when they are the clearest borrowed-view representation.
     */

    struct CompactCompleteConnectionRetention final {
        std::vector<std::vector<ConnectionSegmentId>> traces{};
        std::vector<CompleteConnectionMetrics>        metrics{};
    };

    /**
     * @brief Retained complete-connection state for one projection slot.
     *
     * Complete-alternative state is projection-local. DemandTasks keep
     * timed OD-interval alternatives. OdDayPairs never retain raw complete
     * alternatives: every completed relevant connection is projected to
     * the final DayPathRetention of its OD slot.
     */
    struct SearchProjectionRetention final {
        SearchProjectionSlot slot{};
        NodeMetricMap known_metrics{};
        CompleteConnectionRetention complete_connections{};
        CompactCompleteConnectionRetention compact_complete_connections{};
        DayPathRetention day_paths{};
    };

    /**
     * @brief Tree-level partial retention.
     */
    struct TreePartialRetention final {
        PaperConnectionNodeMetricMap paper_connections{};
        NodeMetricMap                known_metrics{};
        OdDayLabelStateMap           od_day_label_states{};
    };

    struct SearchSlotResult final {
        SearchProjectionSlot             slot{};
        std::size_t                      connection_count{};
        std::vector<SearchConnection>    connections{};
        std::vector<DayPathAlternative>  day_path_alternatives{};
    };

    struct SearchBatchKey final {
        ZoneId                        origin;
        std::optional<IntervalId>      interval{};
        std::vector<SearchTimeWindow> departure_windows{};
    };

    [[nodiscard]] bool operator<(
          const SearchBatchKey& lhs
        , const SearchBatchKey& rhs
    ) noexcept;

    struct SearchBatch final {
        SearchBatchKey                         key{};
        std::reference_wrapper<const SearchTimeDomain> departure_domain;
        std::vector<SearchCompletionTarget>    completion_targets{};
        std::vector<SearchProjectionSlot>      projection_slots{};
    };

    struct SearchBatchExecutionDiagnostics final {
        std::size_t completion_target_count{};
        std::size_t projection_task_count{};
        std::size_t zero_completion_target_tree_count{};
        std::size_t zero_projection_task_tree_count{};
        std::size_t max_completion_targets_per_tree{};
        std::size_t max_projection_tasks_per_tree{};
    };

    [[nodiscard]] SearchBatchExecutionDiagnostics summarize_search_batches(
        std::span<const SearchBatch> batches
    ) noexcept;

    [[nodiscard]] SearchProjectionSlot make_demand_task_projection_slot(
          const SearchTask& task
        , std::size_t       result_index
    ) noexcept;

    mathfp::Expected<std::vector<SearchProjectionSlot>> build_demand_projection_slots(
        std::span<const SearchTask> tasks
    );

    [[nodiscard]] std::vector<SearchProjectionSlot> build_completion_target_projection_slots(
        const SearchTreeJob& job
    );

    [[nodiscard]] std::vector<SearchProjectionSlot> build_od_day_pair_projection_slots(
        const SearchTreeJob& job
    );

    mathfp::Expected<std::vector<SearchBatch>> build_origin_period_search_batches(
          std::span<const SearchTask>    tasks
        , std::span<const SearchTreeJob> tree_jobs
        , SearchResultProjection         result_projection
    );

    [[nodiscard]] std::vector<SearchBatch> build_interval_local_search_batches(
        std::span<const SearchTask> tasks
    );

}  // namespace timetable::domain::assignment
