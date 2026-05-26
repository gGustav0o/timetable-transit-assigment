#include "timetable/domain/assignment/choice/choice.hpp"

#include <cstddef>
#include <iterator>
#include <map>
#include <utility>
#include <vector>

#include <fmt/format.h>

#include <mathfp/core/try.hpp>

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

        struct ChoiceTaskSelection final {
            ChoiceTaskResult result{};
            std::size_t      admissibility_rejected{};
        };

        struct ChoiceOdDaySelection final {
            OdDayChoicePairResult result{};
        };

        std::vector<const SearchConnection*> admissible_task_connection_ptrs(
              const SearchTaskResult&            task_result
            , const AssignmentPeriodConfig&      assignment_period
            , const ConnectionAdmissibilityConfig& admissibility_config
        ) {
            std::vector<const SearchConnection*> connections;
            connections.reserve(task_result.connections.size());

            for (const auto& connection : task_result.connections) {
                if (connection_admissible_for_demand_segment(
                      metrics_of(connection)
                    , task_result.task.interval
                    , assignment_period
                    , admissibility_config
                )) {
                    connections.push_back(&connection);
                }
            }

            return connections;
        }

        mathfp::Expected<ChoiceTaskSelection> choose_task_connections(
              const SearchTaskResult& task_result
            , const SearchParams&     params
            , const SearchCostContext& search_cost
            , const ChoiceConfig&     config
            , const AssignmentPeriodConfig& assignment_period
            , const ConnectionAdmissibilityConfig& admissibility_config
        ) {
            const auto task_connections = admissible_task_connection_ptrs(
                  task_result
                , assignment_period
                , admissibility_config
            );
            MATHFP_TRY_LET(
                  std::vector<SearchConnection>
                , chosen
                , refine_complete_connection_ptrs(
                      // Final choice retention must use the same metric as search retention.
                      task_connections
                    , search_cost
                    , task_result.task.interval.id
                    , params.choice_tolerances
                    , config.rollout_stage
                )
            );
            return ChoiceTaskSelection{
                  .result = ChoiceTaskResult{
                      .task        = task_result.task
                    , .connections = std::move(chosen)
                  }
                , .admissibility_rejected =
                    task_result.connections.size() - task_connections.size()
            };
        }

        std::vector<const SearchConnection*> connection_ptrs(
            const std::vector<SearchConnection>& connections
        ) {
            std::vector<const SearchConnection*> ptrs;
            ptrs.reserve(connections.size());
            for (const auto& connection : connections) {
                ptrs.push_back(&connection);
            }
            return ptrs;
        }

        mathfp::Expected<ChoiceOdDaySelection> choose_od_day_pair_connections(
              const OdDayPairResult&  pair_result
            , const SearchParams&     params
            , const SearchCostContext& search_cost
            , const ChoiceConfig&     config
        ) {
            const auto pair_connections = connection_ptrs(pair_result.connections);
            MATHFP_TRY_LET(
                  std::vector<SearchConnection>
                , chosen
                , refine_complete_connection_ptrs(
                      pair_connections
                    , search_cost
                    , IntervalId{ 0 }
                    , params.choice_tolerances
                    , config.rollout_stage
                )
            );
            return ChoiceOdDaySelection{
                .result = OdDayChoicePairResult{
                      .origin      = pair_result.origin
                    , .destination = pair_result.destination
                    , .connections = std::move(chosen)
                }
            };
        }

    }  // namespace

    mathfp::Expected<ConnectionChoiceResult> choose_connections(
          const ConnectionSearchResult& search_result
        , const SearchParams&           params
        , const SearchCostContext&      search_cost
        , const ChoiceConfig&           config
        , const AssignmentPeriodConfig& assignment_period
        , const ConnectionAdmissibilityConfig& admissibility_config
    ) {
        using timetable::infra::LogLevel;
        using timetable::infra::progress::both;
        using timetable::infra::progress::log;

        MATHFP_TRY(validate_assignment_period_config(assignment_period));
        MATHFP_TRY(validate_connection_admissibility_config(admissibility_config));
        MATHFP_TRY(validate_search_cost_context(search_cost));

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
        std::size_t admissibility_rejected_count = 0;
        for (const auto& task_result : search_result.task_results) {
            MATHFP_TRY_LET(
                  ChoiceTaskSelection
                , selection
                , choose_task_connections(
                      task_result
                    , params
                    , search_cost
                    , config
                    , assignment_period
                    , admissibility_config
                )
            );
            auto chosen_task = std::move(selection.result);
            admissibility_rejected_count += selection.admissibility_rejected;
            if (!chosen_task.connections.empty()) {
                ++nonempty_task_count;
            }
            log(
                fmt::format(
                    "choice task: index = {:>8}  origin = {:>6}  destination = {:>6}"
                    "  interval = {:>6}  input = {:>5}  chosen = {:>5}  admissibility_rejected = {:>5}"
                    , task_result.task.index.get()
                    , task_result.task.origin.get()
                    , task_result.task.destination.get()
                    , task_result.task.interval.id.get()
                    , task_result.connections.size()
                    , chosen_task.connections.size()
                    , selection.admissibility_rejected
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
                  "choice result: nonempty_tasks = {:>8}  connections = {:>8}  admissibility_rejected = {:>8}"
                , nonempty_task_count
                , result.connections.size()
                , admissibility_rejected_count
            )
            , LogLevel::Info
        );
        both("choice: pruning connections done");
        return result;
    }

    mathfp::Expected<OdDayConnectionChoiceResult> choose_od_day_connections(
          const OdDayConnectionSearchResult& search_result
        , const SearchParams&                params
        , const SearchCostContext&           search_cost
        , const ChoiceConfig&                config
    ) {
        using timetable::infra::LogLevel;
        using timetable::infra::progress::both;
        using timetable::infra::progress::log;

        MATHFP_TRY(validate_search_cost_context(search_cost));

        both("choice: pruning OD-day connections");
        log(
            fmt::format(
                  "OD-day choice input: origins = {:>8}  connections = {:>8}  rollout_stage = {}"
                , search_result.origin_results.size()
                , search_connection_count(search_result)
                , to_string(config.rollout_stage)
            )
            , LogLevel::Info
        );

        OdDayConnectionChoiceResult result;
        result.origin_results.reserve(search_result.origin_results.size());
        std::map<detail::grouping::ConnectionTraceKey, std::size_t> chosen_trace_index;
        std::size_t pair_count = 0;
        std::size_t nonempty_pair_count = 0;

        for (const auto& origin_result : search_result.origin_results) {
            OriginDayChoiceResult chosen_origin{
                  .origin       = origin_result.origin
                , .pair_results = {}
            };
            chosen_origin.pair_results.reserve(origin_result.pair_results.size());

            for (const auto& pair_result : origin_result.pair_results) {
                ++pair_count;
                MATHFP_TRY_LET(
                      ChoiceOdDaySelection
                    , selection
                    , choose_od_day_pair_connections(
                          pair_result
                        , params
                        , search_cost
                        , config
                    )
                );
                auto chosen_pair = std::move(selection.result);
                if (!chosen_pair.connections.empty()) {
                    ++nonempty_pair_count;
                }
                log(
                    fmt::format(
                          "OD-day choice pair: origin = {:>6}  destination = {:>6}  input = {:>5}  chosen = {:>5}"
                        , pair_result.origin.get()
                        , pair_result.destination.get()
                        , pair_result.connections.size()
                        , chosen_pair.connections.size()
                    )
                    , LogLevel::Info
                );
                append_unique_choice_connections(
                      result.connections
                    , chosen_trace_index
                    , chosen_pair.connections
                );
                chosen_origin.pair_results.push_back(std::move(chosen_pair));
            }

            result.origin_results.push_back(std::move(chosen_origin));
        }

        log(
            fmt::format(
                  "OD-day choice result: pairs = {:>8}  nonempty_pairs = {:>8}  connections = {:>8}"
                , pair_count
                , nonempty_pair_count
                , result.connections.size()
            )
            , LogLevel::Info
        );
        both("choice: pruning OD-day connections done");
        return result;
    }

    mathfp::Expected<OriginDayChoiceResult> choose_origin_day_connections(
          const OriginDaySearchResult& search_result
        , const SearchParams&          params
        , const SearchCostContext&     search_cost
        , const ChoiceConfig&          config
    ) {
        MATHFP_TRY(validate_search_cost_context(search_cost));

        OriginDayChoiceResult result{
              .origin       = search_result.origin
            , .pair_results = {}
        };
        result.pair_results.reserve(search_result.pair_results.size());

        for (const auto& pair_result : search_result.pair_results) {
            MATHFP_TRY_LET(
                  ChoiceOdDaySelection
                , selection
                , choose_od_day_pair_connections(
                      pair_result
                    , params
                    , search_cost
                    , config
                )
            );
            result.pair_results.push_back(std::move(selection.result));
        }

        return result;
    }

}  // namespace timetable::domain::assignment
