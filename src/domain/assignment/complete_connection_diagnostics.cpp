#include "timetable/domain/assignment/complete_connection_diagnostics.hpp"

#include <fmt/format.h>

#include "detail/diagnostic_format.hpp"

namespace timetable::domain::assignment {

    CompleteConnectionDominanceConfigSummary summarize(
        const CompleteConnectionDominanceConfig& config
    ) noexcept {
        return CompleteConnectionDominanceConfigSummary{
              .deactivate_dominance_of_direct_connections =
                  config.deactivate_dominance_of_direct_connections
            , .direct_connections_can_dominate =
                  !config.deactivate_dominance_of_direct_connections
        };
    }

    std::string format_complete_connection_dominance_config_summary(
        const CompleteConnectionDominanceConfigSummary& summary
    ) {
        return fmt::format(
              "complete-connection dominance config: deactivate_direct_dominance={} direct_connections_can_dominate={}"
            , detail::diagnostic::bool_text(
                  summary.deactivate_dominance_of_direct_connections
              )
            , detail::diagnostic::bool_text(summary.direct_connections_can_dominate)
        );
    }

}  // namespace timetable::domain::assignment
