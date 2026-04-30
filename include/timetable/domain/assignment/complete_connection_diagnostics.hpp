#pragma once

#include <string>

#include "timetable/domain/assignment/complete_connection_retention.hpp"

namespace timetable::domain::assignment {

    struct CompleteConnectionDominanceConfigSummary final {
        bool deactivate_dominance_of_direct_connections{};
        bool direct_connections_can_dominate{ true };
    };

    [[nodiscard]] CompleteConnectionDominanceConfigSummary summarize(
        const CompleteConnectionDominanceConfig& config
    ) noexcept;

    [[nodiscard]] std::string format_complete_connection_dominance_config_summary(
        const CompleteConnectionDominanceConfigSummary& summary
    );

}  // namespace timetable::domain::assignment
