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
#include "timetable/domain/assignment/od_day_path_result.hpp"
#include "timetable/domain/assignment/search/demand.hpp"
#include "timetable/domain/assignment/search/model/retention.hpp"
#include "timetable/domain/assignment/search/problem.hpp"
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

    struct CompactCompleteConnectionRetention final {
        std::vector<std::vector<ConnectionSegmentId>> traces{};
        std::vector<CompleteConnectionMetrics>        metrics{};
    };

    /**
     * @brief Projection-local complete-alternative state.
     */
    struct SearchProjectionRetention final {
        SearchProjectionSlot slot{};
        NodeMetricMap known_metrics{};
        CompleteConnectionRetention complete_connections{};
        CompactCompleteConnectionRetention compact_complete_connections{};
        DayPathRetention day_paths{};
    };

    /**
     * @brief Tree-level partial retention shared by projection slots.
     */
    struct TreePartialRetention final {
        PaperConnectionNodeMetricMap paper_connections{};
        NodeMetricMap                known_metrics{};
        OdDayLabelStateMap           od_day_label_states{};
    };

    struct SearchSlotResult final {
        SearchProjectionSlot            slot{};
        std::size_t                     connection_count{};
        std::vector<SearchConnection>   connections{};
        std::vector<DayPathAlternative> day_path_alternatives{};
    };

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

}  // namespace timetable::domain::assignment
