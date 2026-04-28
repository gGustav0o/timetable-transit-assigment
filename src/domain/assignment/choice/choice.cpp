#include "timetable/domain/assignment/choice/choice.hpp"

#include <cstddef>
#include <iterator>
#include <map>
#include <utility>
#include <vector>

#include <fmt/format.h>

#include "../detail/grouping.hpp"
#include "timetable/domain/assignment/complete_connection_retention.hpp"
#include "timetable/infra/progress_bus.hpp"

namespace timetable::domain::assignment {
    namespace {

        void append_unique_choice_connections(
              std::vector<SearchConnection>&                              target
            , std::map<detail::grouping::ConnectionTraceKey, std::size_t>& seen
            , const std::vector<SearchConnection>&                         source
        ) {
            for (const auto& connection : source) {
                const auto key = detail::grouping::connection_trace_key(connection);
                if (seen.emplace(key, target.size()).second) {
                    target.push_back(connection);
                }
            }
        }

        std::vector<const SearchConnection*> task_connection_ptrs(
            const SearchTaskResult& task_result
        ) {
            std::vector<const SearchConnection*> connections;
            connections.reserve(task_result.connections.size());
            for (const auto& connection : task_result.connections) {
                connections.push_back(&connection);
            }
            return connections;
        }

        ChoiceTaskResult choose_task_connections(
              const SearchTaskResult& task_result
            , const SearchParams&     params
            , double                  fare_scale
            , const ChoiceConfig&     config
        ) {
            const auto task_connections = task_connection_ptrs(task_result);
            auto chosen = refine_complete_connection_ptrs(
                  task_connections
                , params
                , fare_scale
                , config.rollout_stage
            );
            return ChoiceTaskResult{
                  .task        = task_result.task
                , .connections = std::move(chosen)
            };
        }

    }  // namespace

    mathfp::Expected<ConnectionChoiceResult> choose_connections(
          const ConnectionSearchResult& search_result
        , const SearchParams&           params
        , double                        fare_scale
        , const ChoiceConfig&           config
    ) {
        using timetable::infra::LogLevel;
        using timetable::infra::progress::both;
        using timetable::infra::progress::log;

        both("choice: pruning connections");
        log(
            fmt::format(
                  "choice input: tasks = {:>8}  connections = {:>8}  rollout_stage = {}"
                , search_result.task_results.size()
                , search_connection_count(search_result)
                , to_string(config.rollout_stage)
            )
            , LogLevel::Info
        );

        ConnectionChoiceResult result;
        result.task_results.reserve(search_result.task_results.size());
        std::map<detail::grouping::ConnectionTraceKey, std::size_t> chosen_trace_index;
        std::size_t nonempty_task_count = 0;
        for (const auto& task_result : search_result.task_results) {
            auto chosen_task = choose_task_connections(
                  task_result
                , params
                , fare_scale
                , config
            );
            if (!chosen_task.connections.empty()) {
                ++nonempty_task_count;
            }
            log(
                fmt::format(
                    "choice task: index = {:>8}  origin = {:>6}  destination = {:>6}"
                    "  interval = {:>6}  input = {:>5}  chosen = {:>5}"
                    , task_result.task.index.get()
                    , task_result.task.origin.get()
                    , task_result.task.destination.get()
                    , task_result.task.interval.id.get()
                    , task_result.connections.size()
                    , chosen_task.connections.size()
                )
                , LogLevel::Info
            );
            append_unique_choice_connections(
                  result.connections
                , chosen_trace_index
                , chosen_task.connections
            );
            result.task_results.push_back(std::move(chosen_task));
        }

        log(
            fmt::format(
                  "choice result: nonempty_tasks = {:>8}  connections = {:>8}"
                , nonempty_task_count
                , result.connections.size()
            )
            , LogLevel::Info
        );
        both("choice: pruning connections done");
        return result;
    }

}  // namespace timetable::domain::assignment
