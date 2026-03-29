#include "timetable/domain/assignment/validation.hpp"

#include <cstddef>
#include <cstdint>

#include <mathfp/core/error.hpp>
#include <mathfp/core/try.hpp>
#include <mathfp/core/unit.hpp>

#include "../detail/validation_common.hpp"

namespace timetable::domain::assignment {

    mathfp::Expected<mathfp::Unit> validate_choice_step_output(
        const ConnectionChoiceResult& choice_result
        , const ConnectionSearchResult& search_result
    ) {
        if (search_result.connections.empty() && choice_result.connections.empty()) {
            detail::validation::warn("choice output: no search connections were available to prune");
            return mathfp::kUnit;
        }

        MATHFP_TRY(detail::validation::validate_unique_connection_traces(choice_result.connections, "choice"));

        const auto search_trace_map = detail::validation::trace_index_map(search_result.connections);
        for (std::size_t i = 0; i < choice_result.connections.size(); ++i) {
            const auto& connection = choice_result.connections[i];
            if (!search_trace_map.contains(detail::validation::connection_trace_key(connection))) {
                return mathfp::unexpected(
                    mathfp::internal_error("choice output contains a connection that was not present in search output")
                        .ctx("choice_index", static_cast<std::int64_t>(i))
                        .ctx("origin", connection.origin.get())
                        .ctx("destination", connection.destination.get())
                );
            }
        }

        const auto search_groups = detail::validation::count_connections_by_od(search_result.connections);
        const auto choice_groups = detail::validation::count_connections_by_od(choice_result.connections);
        for (const auto& [od, search_count] : search_groups) {
            if (search_count > 0 && !choice_groups.contains(od)) {
                return mathfp::unexpected(
                    mathfp::internal_error("choice step removed every connection from a non-empty OD group")
                        .ctx("origin", od.origin.get())
                        .ctx("destination", od.destination.get())
                        .ctx("search_count", static_cast<std::int64_t>(search_count))
                );
            }
        }

        if (choice_result.connections.empty()) {
            detail::validation::warn("choice output: every searched OD group is empty after pruning");
        }

        return mathfp::kUnit;
    }

}  // namespace timetable::domain::assignment
