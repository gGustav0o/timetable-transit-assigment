#include "timetable/domain/assignment/validation.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <vector>

#include <fmt/format.h>

#include <mathfp/core/error.hpp>
#include <mathfp/core/try.hpp>
#include <mathfp/core/unit.hpp>

#include "../detail/validation_common.hpp"

namespace timetable::domain::assignment {

    mathfp::Expected<mathfp::Unit> validate_choice_step_output(
          const ConnectionChoiceResult& choice_result
        , const ConnectionSearchResult& search_result
        , const AssignmentPeriodConfig& assignment_period
        , const ConnectionAdmissibilityConfig& admissibility_config
    ) {
        MATHFP_TRY(validate_assignment_period_config(assignment_period));
        MATHFP_TRY(validate_connection_admissibility_config(admissibility_config));

        if (search_connection_count(search_result) == 0 && choice_result.connections.empty()) {
            detail::validation::warn("choice output: no search connections were available to prune");
        }

        MATHFP_TRY(detail::validation::validate_unique_connection_traces(choice_result.connections, "choice"));

        const auto search_connections = search_connection_ptrs(search_result);
        const auto search_trace_map = detail::validation::trace_index_map(search_connections);
        const auto choice_trace_map = detail::validation::trace_index_map(choice_result.connections);
        MATHFP_TRY(detail::validation::validate_each_index(
              choice_result.connections
            , [&](const SearchConnection& connection, std::size_t i) {
                return detail::validation::ensure_contains(
                      search_trace_map
                    , detail::validation::connection_trace_key(connection)
                    , [&]() {
                        return mathfp::internal_error(
                            "choice output contains a connection that was not present in search output"
                        )
                            .ctx("choice_index", static_cast<std::int64_t>(i))
                            .ctx("origin"      , origin_of(connection).get())
                            .ctx("destination" , destination_of(connection).get());
                    }
                );
            }
        ));

        std::map<detail::validation::ConnectionTraceKey, bool> task_choice_traces;
        std::map<SearchTaskRef, const SearchTaskResult*> search_task_by_ref;
        for (const auto& task_result : search_result.task_results) {
            MATHFP_TRY(detail::validation::emplace_unique(
                  search_task_by_ref
                , task_result.task.index
                , &task_result
                , [&]() {
                    return mathfp::internal_error("search output contains duplicate task refs")
                        .ctx("task", task_result.task.index.get());
                }
            ));
        }

        std::map<SearchTaskRef, const ChoiceTaskResult*> choice_task_by_ref;
        for (const auto& task_result : choice_result.task_results) {
            MATHFP_TRY(detail::validation::emplace_unique(
                  choice_task_by_ref
                , task_result.task.index
                , &task_result
                , [&]() {
                    return mathfp::internal_error("choice output contains duplicate task refs")
                        .ctx("task", task_result.task.index.get());
                }
            ));

            for (const auto& connection : task_result.connections) {
                task_choice_traces[detail::validation::connection_trace_key(connection)] = true;
            }

            const auto search_task_it = search_task_by_ref.find(task_result.task.index);
            if (search_task_it == search_task_by_ref.end()) {
                return mathfp::unexpected(
                    mathfp::internal_error("choice output contains task absent from search output")
                        .ctx("task"       , task_result.task.index.get())
                        .ctx("origin"     , task_result.task.origin.get())
                        .ctx("destination", task_result.task.destination.get())
                );
            }
            const auto& search_task_result = *search_task_it->second;
            if (task_result.task.origin != search_task_result.task.origin
                || task_result.task.destination != search_task_result.task.destination
                || task_result.task.interval.id != search_task_result.task.interval.id) {
                return mathfp::unexpected(
                    mathfp::internal_error("choice task metadata disagrees with search task metadata")
                        .ctx("task", task_result.task.index.get())
                );
            }

            std::vector<const SearchConnection*> search_task_connections;
            search_task_connections.reserve(search_task_result.connections.size());
            for (const auto& connection : search_task_result.connections) {
                search_task_connections.push_back(&connection);
            }
            const auto search_task_trace_map = detail::validation::trace_index_map(search_task_connections);
            MATHFP_TRY(detail::validation::validate_each_index(
                  task_result.connections
                , [&](const SearchConnection& connection, std::size_t i)
                    -> mathfp::Expected<mathfp::Unit> {
                    MATHFP_TRY(detail::validation::ensure_contains(
                          choice_trace_map
                        , detail::validation::connection_trace_key(connection)
                        , [&]() {
                            return mathfp::internal_error(
                                "choice task contains a connection absent from the flat choice projection"
                            )
                                .ctx("task"        , task_result.task.index.get())
                                .ctx("choice_index", static_cast<std::int64_t>(i))
                                .ctx("origin"      , origin_of(connection).get())
                                .ctx("destination" , destination_of(connection).get());
                        }
                    ));
                    if (!connection_admissible_for_demand_segment(
                          metrics_of(connection)
                        , task_result.task.interval
                        , assignment_period
                        , admissibility_config
                    )) {
                        return mathfp::unexpected(
                            mathfp::internal_error("choice task contains inadmissible connection")
                                .ctx("task"        , task_result.task.index.get())
                                .ctx("choice_index", static_cast<std::int64_t>(i))
                                .ctx("origin"      , origin_of(connection).get())
                                .ctx("destination" , destination_of(connection).get())
                                .ctx("interval_id" , task_result.task.interval.id.get())
                        );
                    }
                    return detail::validation::ensure_contains(
                          search_task_trace_map
                        , detail::validation::connection_trace_key(connection)
                        , [&]() {
                            return mathfp::internal_error(
                                "choice task contains a connection that was not present in the corresponding search task"
                            )
                                .ctx("task"        , task_result.task.index.get())
                                .ctx("choice_index", static_cast<std::int64_t>(i))
                                .ctx("origin"      , origin_of(connection).get())
                                .ctx("destination" , destination_of(connection).get());
                        }
                    );
                }
            ));
        }

        if (choice_task_by_ref.size() != search_task_by_ref.size()) {
            return mathfp::unexpected(
                mathfp::internal_error("choice task result count disagrees with search task result count")
                    .ctx("search_tasks", static_cast<std::int64_t>(search_task_by_ref.size()))
                    .ctx("choice_tasks", static_cast<std::int64_t>(choice_task_by_ref.size()))
            );
        }

        for (const auto& task_result : search_result.task_results) {
            const auto choice_task_it = choice_task_by_ref.find(task_result.task.index);
            if (choice_task_it == choice_task_by_ref.end()) {
                return mathfp::unexpected(
                    mathfp::internal_error("choice output is missing a search task")
                        .ctx("task"        , task_result.task.index.get())
                        .ctx("origin"      , task_result.task.origin.get())
                        .ctx("destination" , task_result.task.destination.get())
                        .ctx("interval_id" , task_result.task.interval.id.get())
                        .ctx("search_count", static_cast<std::int64_t>(task_result.connections.size()))
                );
            }
            if (!task_result.connections.empty() && choice_task_it->second->connections.empty()) {
                detail::validation::warn(
                    fmt::format(
                          "choice output: task {} origin={} destination={} interval={} has no retained connections after choice/admissibility filtering"
                        , task_result.task.index.get()
                        , task_result.task.origin.get()
                        , task_result.task.destination.get()
                        , task_result.task.interval.id.get()
                    )
                );
            }
        }

        for (const auto& connection : choice_result.connections) {
            MATHFP_TRY(detail::validation::ensure_contains(
                  task_choice_traces
                , detail::validation::connection_trace_key(connection)
                , [&]() {
                    return mathfp::internal_error(
                        "flat choice projection contains a connection absent from all choice tasks"
                    )
                        .ctx("origin"     , origin_of(connection).get())
                        .ctx("destination", destination_of(connection).get());
                }
            ));
        }

        if (choice_result.connections.empty()) {
            detail::validation::warn("choice output: every searched task is empty after pruning");
        }

        return mathfp::kUnit;
    }

}  // namespace timetable::domain::assignment
