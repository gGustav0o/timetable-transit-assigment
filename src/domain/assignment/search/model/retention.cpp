#include "timetable/domain/assignment/search/model/retention.hpp"

namespace timetable::domain::assignment {

    PaperConnectionLabelId allocate_paper_connection_label(
          PaperConnectionLabelRegistry&         registry
        , std::optional<PaperConnectionLabelId> parent
    ) {
        const auto id = PaperConnectionLabelId{ .value = registry.active.size() };
        registry.active.push_back(true);
        registry.parent.push_back(parent);
        return id;
    }

    void deactivate_paper_connection_label(
          PaperConnectionLabelRegistry& registry
        , PaperConnectionLabelId        label
    ) noexcept {
        if (label.value < registry.active.size()) {
            registry.active[label.value] = false;
        }
    }

    bool paper_connection_label_active(
          const PaperConnectionLabelRegistry& registry
        , std::optional<PaperConnectionLabelId> label
    ) noexcept {
        if (!label.has_value()) {
            return true;
        }
        auto cursor = label;
        while (cursor.has_value()) {
            if (cursor->value >= registry.active.size()
                || cursor->value >= registry.parent.size()
                || !registry.active[cursor->value]) {
                return false;
            }
            cursor = registry.parent[cursor->value];
        }
        return true;
    }

    OdDayLabelRetentionConfig od_day_label_retention_config_of(
        const SearchExecutionConfig& config
    ) noexcept {
        return OdDayLabelRetentionConfig{
            .max_representatives_per_label =
                config.max_od_day_label_representatives_per_state
        };
    }

}  // namespace timetable::domain::assignment
