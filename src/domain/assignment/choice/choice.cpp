#include "timetable/domain/assignment/choice/choice.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <map>
#include <span>
#include <utility>
#include <vector>

#include <fmt/format.h>

#include <mathfp/core/error.hpp>
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

        struct ChoiceOdDayPathSelection final {
            OdDayPathChoicePairResult result{};
        };

        CompleteConnectionMetricSummary summarize_day_path_alternative_supports(
            const DayPathAlternative& alternative
        ) noexcept {
            CompleteConnectionMetricSummary summary{
                  .min_impedance    = alternative.support.representative_metrics.impedance
                , .min_journey_time =
                      alternative.support.representative_metrics.journey_time.value()
                , .min_transfers    = static_cast<double>(
                      alternative.support.representative_metrics.transfers.get()
                  )
                , .empty            = false
            };
            for (const auto& support : day_path_split_support_descriptors(alternative)) {
                summary.min_impedance = std::min(
                      summary.min_impedance
                    , support.complete_metrics.impedance
                );
                summary.min_journey_time = std::min(
                      summary.min_journey_time
                    , support.complete_metrics.journey_time.value()
                );
                summary.min_transfers = std::min(
                      summary.min_transfers
                    , static_cast<double>(support.complete_metrics.transfers.get())
                );
            }
            return summary;
        }

        bool day_path_alternative_within_tolerances(
              const DayPathAlternative&         alternative
            , const CompleteConnectionMetricSummary& global_summary
            , const ChoiceTolerances&           tolerances
        ) noexcept {
            if (within_complete_connection_tolerances(
                  alternative.support.representative_metrics
                , global_summary
                , tolerances
            )) {
                return true;
            }
            for (const auto& support : day_path_split_support_descriptors(alternative)) {
                if (within_complete_connection_tolerances(
                      support.complete_metrics
                    , global_summary
                    , tolerances
                )) {
                    return true;
                }
            }
            return false;
        }

        CompleteConnectionMetricSummary summarize_day_path_alternatives(
            std::span<const DayPathAlternative> alternatives
        ) noexcept {
            CompleteConnectionMetricSummary summary{
                  .min_impedance    = std::numeric_limits<double>::infinity()
                , .min_journey_time = std::numeric_limits<double>::infinity()
                , .min_transfers    = std::numeric_limits<double>::infinity()
                , .empty            = alternatives.empty()
            };
            for (const auto& alternative : alternatives) {
                const auto alternative_summary =
                    summarize_day_path_alternative_supports(alternative);
                summary.min_impedance = std::min(
                      summary.min_impedance
                    , alternative_summary.min_impedance
                );
                summary.min_journey_time = std::min(
                      summary.min_journey_time
                    , alternative_summary.min_journey_time
                );
                summary.min_transfers = std::min(
                      summary.min_transfers
                    , alternative_summary.min_transfers
                );
            }
            return summary;
        }

        std::vector<DayPathAlternative> choose_day_path_alternatives(
              std::vector<DayPathAlternative> alternatives
            , const ChoiceTolerances&         tolerances
            , ChoiceRolloutStage              rollout_stage
        ) {
            if (rollout_stage == ChoiceRolloutStage::ExactOnly) {
                return alternatives;
            }

            const auto summary = summarize_day_path_alternatives(
                std::span<const DayPathAlternative>{
                      alternatives.data()
                    , alternatives.size()
                }
            );
            alternatives.erase(
                  std::remove_if(
                        alternatives.begin()
                      , alternatives.end()
                      , [&](const DayPathAlternative& alternative) {
                            return !day_path_alternative_within_tolerances(
                                  alternative
                                , summary
                                , tolerances
                            );
                        }
                    )
                , alternatives.end()
            );
            return alternatives;
        }

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

        mathfp::Expected<ChoiceOdDayPathSelection> choose_od_day_pair_connections(
              const OdDayPairResult&  pair_result
            , const SearchParams&     params
            , const SearchCostContext& search_cost
            , const ChoiceConfig&     config
        ) {
            (void)search_cost;
            std::map<DayPathSignature, bool> signatures;
            for (std::size_t i = 0; i < pair_result.alternatives.size(); ++i) {
                const auto& alternative = pair_result.alternatives[i];
                MATHFP_TRY(validate_day_path_alternative(alternative, i));
                if (!signatures.emplace(day_path_signature_of(alternative), true).second) {
                    return mathfp::unexpected(
                        mathfp::internal_error("OD-day choice input contains duplicate day-path alternatives")
                            .ctx("origin", pair_result.origin.get())
                            .ctx("destination", pair_result.destination.get())
                    );
                }
            }
            auto alternatives = choose_day_path_alternatives(
                  pair_result.alternatives
                , params.choice_tolerances
                , config.rollout_stage
            );
            auto representatives = day_path_representative_connections(
                std::span<const DayPathAlternative>{
                      alternatives.data()
                    , alternatives.size()
                }
            );
            return ChoiceOdDayPathSelection{
                .result = OdDayPathChoicePairResult{
                      .origin      = pair_result.origin
                    , .destination = pair_result.destination
                    , .alternatives = std::move(alternatives)
                    , .connections = std::move(representatives)
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

    mathfp::Expected<OdDayPathChoiceResult> choose_od_day_paths(
          const OdDayPathSearchResult& search_result
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

        OdDayPathChoiceResult result;
        result.origin_results.reserve(search_result.origin_results.size());
        std::map<detail::grouping::ConnectionTraceKey, std::size_t> chosen_trace_index;
        std::size_t pair_count = 0;
        std::size_t nonempty_pair_count = 0;

        for (const auto& origin_result : search_result.origin_results) {
            OriginDayPathChoiceResult chosen_origin{
                  .origin       = origin_result.origin
                , .pair_results = {}
            };
            chosen_origin.pair_results.reserve(origin_result.pair_results.size());

            for (const auto& pair_result : origin_result.pair_results) {
                ++pair_count;
                MATHFP_TRY_LET(
                      ChoiceOdDayPathSelection
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
                        , pair_result.alternatives.size()
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

    mathfp::Expected<OriginDayPathChoiceResult> choose_origin_day_paths(
          const OriginDaySearchResult& search_result
        , const SearchParams&          params
        , const SearchCostContext&     search_cost
        , const ChoiceConfig&          config
    ) {
        MATHFP_TRY(validate_search_cost_context(search_cost));

        OriginDayPathChoiceResult result{
              .origin       = search_result.origin
            , .pair_results = {}
        };
        result.pair_results.reserve(search_result.pair_results.size());

        for (const auto& pair_result : search_result.pair_results) {
            MATHFP_TRY_LET(
                  ChoiceOdDayPathSelection
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

    mathfp::Expected<OdDayConnectionChoiceResult> choose_od_day_connections(
          const OdDayPathSearchResult& search_result
        , const SearchParams&          params
        , const SearchCostContext&     search_cost
        , const ChoiceConfig&          config
    ) {
        return choose_od_day_paths(
              search_result
            , params
            , search_cost
            , config
        );
    }

    mathfp::Expected<OriginDayChoiceResult> choose_origin_day_connections(
          const OriginDaySearchResult& search_result
        , const SearchParams&          params
        , const SearchCostContext&     search_cost
        , const ChoiceConfig&          config
    ) {
        return choose_origin_day_paths(
              search_result
            , params
            , search_cost
            , config
        );
    }

}  // namespace timetable::domain::assignment
