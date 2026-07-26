#include "timetable/domain/assignment/search/model/retention.hpp"

namespace timetable::domain::assignment {

    RetainedConnectionLabelId allocate_retained_connection_label(
          RetainedConnectionLabelRegistry&         registry
        , std::optional<RetainedConnectionLabelId> parent
    ) {
        const auto id = RetainedConnectionLabelId{ .value = registry.active.size() };
        registry.active.push_back(true);
        registry.parent.push_back(parent);
        return id;
    }

    void deactivate_retained_connection_label(
          RetainedConnectionLabelRegistry& registry
        , RetainedConnectionLabelId        label
    ) noexcept {
        if (label.value < registry.active.size()) {
            registry.active[label.value] = false;
        }
    }

    bool retained_connection_label_active(
          const RetainedConnectionLabelRegistry& registry
        , std::optional<RetainedConnectionLabelId> label
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

}  // namespace timetable::domain::assignment
