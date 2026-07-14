#include "timetable/domain/assignment/search/search.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <mathfp/core/error.hpp>
#include <mathfp/core/try.hpp>
#include <mathfp/core/unit.hpp>

#include "timetable/domain/assignment/complete_connection_retention.hpp"
#include "timetable/domain/assignment/connection.hpp"
#include "timetable/domain/assignment/search_cost.hpp"
#include "timetable/domain/assignment/search/runtime/search_runtime.hpp"
#include "timetable/domain/assignment/validation.hpp"

namespace timetable::domain::assignment {
    namespace {

        [[nodiscard]] mathfp::Expected<std::map<IntervalId, const TimeInterval*>> interval_lookup(
            const InputModel& input
        ) {
            std::map<IntervalId, const TimeInterval*> lookup;
            for (const auto& interval : input.intervals) {
                if (!lookup.emplace(interval.id, &interval).second) {
                    return mathfp::unexpected(
                        mathfp::invalid_arg("duplicate interval id while building search tasks")
                            .ctx("interval_id", interval.id.get())
                    );
                }
            }
            return lookup;
        }

        [[nodiscard]] std::vector<ConnectionSegmentId> connection_segments_of(
            const ConnectionTrace& trace
        ) {
            std::vector<ConnectionSegmentId> segments;
            segments.reserve(trace.legs.size());
            for (const auto& leg : trace.legs) {
                if (leg.connection_segment.has_value()) {
                    segments.push_back(*leg.connection_segment);
                }
            }
            return segments;
        }

    }  // namespace

    SearchConnection::SearchConnection(
        Connection connection
    )
        : connection_(std::move(connection)) {}

    mathfp::Expected<std::vector<SearchTask>> build_search_tasks(
        const InputModel& input
    ) {
        auto intervals_result = interval_lookup(input);
        if (!intervals_result) {
            return mathfp::unexpected(std::move(intervals_result.error()));
        }
        auto intervals = std::move(*intervals_result);
        const auto task_departure_padding = SearchTimePadding{};
        std::vector<SearchTask> tasks;
        tasks.reserve(input.demand.size());

        for (const auto& demand : input.demand) {
            if (!(demand.passengers > 0.0)) {
                continue;
            }

            const auto interval_it = intervals.find(demand.interval);
            if (interval_it == intervals.end()) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("search task demand entry references unknown interval")
                        .ctx("origin"     , demand.origin     .get())
                        .ctx("destination", demand.destination.get())
                        .ctx("interval_id", demand.interval   .get())
                );
            }

            MATHFP_TRY_LET(
                  SearchTimeDomain
                , departure_domain
                , make_search_time_domain(std::vector<SearchTimeWindow>{
                    expand_interval_to_search_window(
                          *interval_it->second
                        , task_departure_padding
                    )
                })
            );

            tasks.push_back(
                SearchTask{
                      .index            = SearchTaskRef{ static_cast<std::int64_t>(tasks.size()) }
                    , .origin           = demand.origin
                    , .destination      = demand.destination
                    , .interval         = *interval_it->second
                    , .departure_domain = std::move(departure_domain)
                }
            );
        }

        return tasks;
    }

    std::vector<SearchCompletionTarget> make_search_completion_targets(
        const std::map<ZoneId, bool>& destinations
    ) {
        std::vector<SearchCompletionTarget> targets;
        targets.reserve(destinations.size());
        for (const auto& [destination, _] : destinations) {
            targets.push_back(
                SearchCompletionTarget{
                      .index = SearchCompletionTargetRef{
                          static_cast<std::int64_t>(targets.size())
                      }
                    , .destination = destination
                }
            );
        }
        return targets;
    }

    mathfp::Expected<std::vector<SearchTreeJob>> build_origin_period_search_tree_jobs(
          std::span<const SearchTask> tasks
        , const SearchTimeDomain&     period_domain
    ) {
        MATHFP_TRY(validate_search_time_domain(period_domain));

        struct MutableOriginJob final {
            std::vector<SearchTaskRef> projection_tasks{};
            std::map<ZoneId, bool>     completion_destinations{};
        };

        std::map<ZoneId, MutableOriginJob> grouped;
        for (const auto& task : tasks) {
            auto& job = grouped[task.origin];
            job.projection_tasks.push_back(task.index);
            job.completion_destinations.emplace(task.destination, true);
        }

        std::vector<SearchTreeJob> jobs;
        jobs.reserve(grouped.size());
        for (auto& [origin, job] : grouped) {
            jobs.push_back(
                SearchTreeJob{
                      .index = SearchTreeJobRef{ static_cast<std::int64_t>(jobs.size()) }
                    , .origin = origin
                    , .departure_domain = period_domain
                    , .completion_targets = make_search_completion_targets(
                          job.completion_destinations
                      )
                    , .projection_tasks = std::move(job.projection_tasks)
                }
            );
        }

        return jobs;
    }

    mathfp::Expected<std::vector<SearchTreeJob>> build_origin_period_search_tree_jobs(
          std::span<const SearchTask>       tasks
        , const SearchTimeDomainExecution&  execution
        , SearchDestinationScope            destination_scope
        , std::span<const Zone>             declared_zones
    ) {
        MATHFP_TRY(validate_search_time_domain_execution(execution));
        std::map<ZoneId, mathfp::Unit> declared_destination_ids;
        for (const auto& zone : declared_zones) {
            const auto [_, inserted] = declared_destination_ids.emplace(
                  zone.id
                , mathfp::kUnit
            );
            if (!inserted) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("origin-period search contains duplicate declared destination zone")
                        .ctx("zone", zone.id.get())
                );
            }
        }

        struct MutableOriginJob final {
            std::vector<SearchTaskRef> projection_tasks{};
            std::map<ZoneId, bool>     completion_destinations{};
        };

        std::map<ZoneId, MutableOriginJob> grouped;
        for (const auto& task : tasks) {
            auto& job = grouped[task.origin];
            if (destination_scope == SearchDestinationScope::DeclaredZones
                && !declared_destination_ids.contains(task.destination)) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("origin-period search task references destination outside declared destination scope")
                        .ctx("destination", task.destination.get())
                        .ctx("task", task.index.get())
                );
            }
            job.projection_tasks.push_back(task.index);
            job.completion_destinations.emplace(task.destination, true);
        }

        switch (destination_scope) {
            case SearchDestinationScope::DemandDestinations:
                break;

            case SearchDestinationScope::DeclaredZones:
                if (declared_destination_ids.empty()) {
                    return mathfp::unexpected(
                        mathfp::invalid_arg("origin-period search with declared destination scope requires declared zones")
                    );
                }
                for (auto& entry : grouped) {
                    auto& job = entry.second;
                    for (const auto& [destination, _] : declared_destination_ids) {
                        job.completion_destinations.emplace(destination, true);
                    }
                }
                break;
        }

        std::vector<SearchTreeJob> jobs;
        jobs.reserve(grouped.size());
        for (auto& [origin, job] : grouped) {
            const auto* domain = find_origin_search_time_domain(execution, origin);
            if (domain == nullptr) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("origin-period search has no executable search-time domain for active origin")
                        .ctx("origin", origin.get())
                        .ctx("source_mode", std::string(to_string(execution.source_mode)))
                        .ctx("adaptation", std::string(to_string(execution.adaptation)))
                );
            }
            jobs.push_back(
                SearchTreeJob{
                      .index = SearchTreeJobRef{ static_cast<std::int64_t>(jobs.size()) }
                    , .origin = origin
                    , .departure_domain = *domain
                    , .completion_targets = make_search_completion_targets(
                          job.completion_destinations
                      )
                    , .projection_tasks = std::move(job.projection_tasks)
                }
            );
        }

        return jobs;
    }

    mathfp::Expected<std::vector<SearchTreeJob>> build_origin_period_search_tree_jobs(
          std::span<const SearchTask>       tasks
        , const SearchTimeDomainExecution&  execution
    ) {
        return build_origin_period_search_tree_jobs(
              tasks
            , execution
            , SearchDestinationScope::DemandDestinations
            , std::span<const Zone>{}
        );
    }

    mathfp::Expected<std::vector<SearchTreeJob>> build_declared_origin_period_search_tree_jobs(
          std::span<const Zone>             declared_zones
        , std::span<const SearchTask>       tasks
        , const SearchTimeDomainExecution&  execution
        , SearchDestinationScope            destination_scope
    ) {
        MATHFP_TRY(validate_search_time_domain_execution(execution));

        struct MutableOriginJob final {
            std::vector<SearchTaskRef> projection_tasks{};
            std::map<ZoneId, bool>     completion_destinations{};
        };

        std::map<ZoneId, MutableOriginJob> grouped;
        for (const auto& zone : declared_zones) {
            const auto [_, inserted] = grouped.emplace(zone.id, MutableOriginJob{});
            if (!inserted) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("declared origin-period search contains duplicate zone")
                        .ctx("zone", zone.id.get())
                );
            }
        }

        switch (destination_scope) {
            case SearchDestinationScope::DemandDestinations:
                break;

            case SearchDestinationScope::DeclaredZones:
                for (auto& entry : grouped) {
                    auto& job = entry.second;
                    for (const auto& zone : declared_zones) {
                        job.completion_destinations.emplace(zone.id, true);
                    }
                }
                break;
        }

        for (const auto& task : tasks) {
            auto it = grouped.find(task.origin);
            if (it == grouped.end()) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("declared origin-period search task references origin outside declared zones")
                        .ctx("origin", task.origin.get())
                        .ctx("task", task.index.get())
                    );
            }
            if (destination_scope == SearchDestinationScope::DeclaredZones
                && !grouped.contains(task.destination)) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("declared origin-period search task references destination outside declared zones")
                        .ctx("destination", task.destination.get())
                        .ctx("task", task.index.get())
                );
            }
            auto& job = it->second;
            job.projection_tasks.push_back(task.index);
            job.completion_destinations.emplace(task.destination, true);
        }

        std::vector<SearchTreeJob> jobs;
        jobs.reserve(grouped.size());
        for (auto& [origin, job] : grouped) {
            const auto* domain = find_origin_search_time_domain(execution, origin);
            if (domain == nullptr) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("declared origin-period search has no executable search-time domain for origin")
                        .ctx("origin", origin.get())
                        .ctx("source_mode", std::string(to_string(execution.source_mode)))
                        .ctx("adaptation", std::string(to_string(execution.adaptation)))
                );
            }

            jobs.push_back(
                SearchTreeJob{
                      .index = SearchTreeJobRef{ static_cast<std::int64_t>(jobs.size()) }
                    , .origin = origin
                    , .departure_domain = *domain
                    , .completion_targets = make_search_completion_targets(
                          job.completion_destinations
                      )
                    , .projection_tasks = std::move(job.projection_tasks)
                }
            );
        }

        return jobs;
    }

    std::size_t all_zone_target_connection_count(
        const AllZoneTargetResult& target
    ) noexcept {
        if (target.connection_count == 0u && !target.connections.empty()) {
            return target.connections.size();
        }
        return target.connection_count;
    }

    std::vector<const SearchConnection*> search_connection_ptrs(
        const ConnectionSearchResult& result
    ) {
        std::vector<const SearchConnection*> connections;
        const auto total = search_connection_count(result);
        connections.reserve(total);
        for (const auto& task_result : result.task_results) {
            for (const auto& connection : task_result.connections) {
                connections.push_back(&connection);
            }
        }
        return connections;
    }

    std::size_t search_connection_count(
        const ConnectionSearchResult& result
    ) noexcept {
        std::size_t total = 0;
        for (const auto& task_result : result.task_results) {
            total += task_result.connections.size();
        }
        return total;
    }

    std::size_t search_connection_count(
        const AllZoneConnectionSearchResult& result
    ) noexcept {
        std::size_t total = 0;
        for (const auto& tree_result : result.tree_results) {
            for (const auto& target_result : tree_result.target_results) {
                total += all_zone_target_connection_count(target_result);
            }
        }
        return total;
    }

    std::size_t search_connection_count(
        const OdDayPathSearchResult& result
    ) noexcept {
        std::size_t total = 0;
        for (const auto& origin_result : result.origin_results) {
            for (const auto& pair_result : origin_result.pair_results) {
                total += pair_result.alternatives.size();
            }
        }
        return total;
    }

    std::size_t search_connection_count(
        const OdDayPathSearchSummary& summary
    ) noexcept {
        std::size_t total = 0;
        for (const auto& pair_count : summary.pair_counts) {
            total += pair_count.connection_count;
        }
        return total;
    }

    mathfp::Expected<SearchConnection> make_search_connection(
        Connection connection
    ) {
        return make_search_connection(
              connection.origin
            , connection.destination
            , std::move(connection.trace)
        );
    }

    mathfp::Expected<SearchConnection> make_search_connection(
          ZoneId          origin
        , ZoneId          destination
        , ConnectionTrace trace
    ) {
        MATHFP_TRY_LET(
              Connection
            , connection
            , make_connection(origin, destination, std::move(trace))
        );
        return SearchConnection{ std::move(connection) };
    }

    const Connection& canonical_connection(
        const SearchConnection& connection
    ) noexcept {
        return connection.connection_;
    }

    ZoneId origin_of(
        const SearchConnection& connection
    ) noexcept {
        return canonical_connection(connection).origin;
    }

    ZoneId destination_of(
        const SearchConnection& connection
    ) noexcept {
        return canonical_connection(connection).destination;
    }

    ConnectionMetrics metrics_of(
        const SearchConnection& connection
    ) {
        return *compute_connection_metrics(canonical_connection(connection));
    }

    Time departure_time_of(
        const SearchConnection& connection
    ) {
        return metrics_of(connection).departure_time;
    }

    Time arrival_time_of(
        const SearchConnection& connection
    ) {
        return metrics_of(connection).arrival_time;
    }

    Time journey_time_of(
        const SearchConnection& connection
    ) {
        return metrics_of(connection).journey_time;
    }

    Time transfer_time_of(
        const SearchConnection& connection
    ) {
        const auto metrics = metrics_of(connection);
        return metrics.transfer_wait_time + metrics.transfer_walk_time;
    }

    TransferCount transfer_count_of(
        const SearchConnection& connection
    ) {
        return metrics_of(connection).transfer_count;
    }

    double fare_of(
        const SearchConnection& connection
    ) {
        return metrics_of(connection).fare;
    }

    std::vector<ConnectionSegmentId> connection_segment_trace(
        const SearchConnection& connection
    ) {
        return connection_segments_of(canonical_connection(connection).trace);
    }

    mathfp::Expected<ConnectionSearchResult> search_connections_branch_and_bound(
          const PreprocessedNetwork& network
        , std::span<const SearchTask> tasks
        , SearchExecutionRequest     execution
        , const SearchParams&        params
        , const SearchCostContext&   search_cost
        , const ChoiceConfig&         choice_config
        , const AssignmentPeriodConfig& assignment_period
        , const ConnectionAdmissibilityConfig& admissibility_config
        , const SearchPruningExecutionPlan* pruning_execution
        , const CompleteConnectionDominanceConfig& complete_connection_dominance
        , SearchDiagnosticsContext diagnostics
    ) {
        return runtime::search_connections_branch_and_bound(
              network
            , tasks
            , execution
            , params
            , search_cost
            , choice_config
            , assignment_period
            , admissibility_config
            , pruning_execution
            , complete_connection_dominance
            , diagnostics
        );
    }

    mathfp::Expected<AllZoneConnectionSearchResult> search_all_zone_connections_branch_and_bound(
          const PreprocessedNetwork& network
        , std::span<const SearchTask> tasks
        , SearchExecutionRequest     execution
        , const SearchParams&        params
        , const SearchCostContext&   search_cost
        , const ChoiceConfig&         choice_config
        , const AssignmentPeriodConfig& assignment_period
        , const ConnectionAdmissibilityConfig& admissibility_config
        , const SearchPruningExecutionPlan* pruning_execution
        , const CompleteConnectionDominanceConfig& complete_connection_dominance
        , SearchDiagnosticsContext diagnostics
    ) {
        return runtime::search_all_zone_connections_branch_and_bound(
              network
            , tasks
            , execution
            , params
            , search_cost
            , choice_config
            , assignment_period
            , admissibility_config
            , pruning_execution
            , complete_connection_dominance
            , diagnostics
        );
    }

    mathfp::Expected<mathfp::Unit> search_od_day_paths_by_origin_branch_and_bound(
          const PreprocessedNetwork& network
        , std::span<const SearchTask> tasks
        , SearchExecutionRequest     execution
        , const SearchParams&        params
        , const SearchCostContext&   search_cost
        , const ChoiceConfig&         choice_config
        , const AssignmentPeriodConfig& assignment_period
        , const ConnectionAdmissibilityConfig& admissibility_config
        , const SearchPruningExecutionPlan* pruning_execution
        , const CompleteConnectionDominanceConfig& complete_connection_dominance
        , OdDayOriginResultSink      origin_sink
        , SearchDiagnosticsContext diagnostics
    ) {
        return runtime::search_od_day_paths_by_origin_branch_and_bound(
              network
            , tasks
            , execution
            , params
            , search_cost
            , choice_config
            , assignment_period
            , admissibility_config
            , pruning_execution
            , complete_connection_dominance
            , std::move(origin_sink)
            , diagnostics
        );
    }
    mathfp::Expected<OdDayPathSearchResult> search_od_day_paths_branch_and_bound(
          const PreprocessedNetwork& network
        , std::span<const SearchTask> tasks
        , SearchExecutionRequest     execution
        , const SearchParams&        params
        , const SearchCostContext&   search_cost
        , const ChoiceConfig&         choice_config
        , const AssignmentPeriodConfig& assignment_period
        , const ConnectionAdmissibilityConfig& admissibility_config
        , const SearchPruningExecutionPlan* pruning_execution
        , const CompleteConnectionDominanceConfig& complete_connection_dominance
        , SearchDiagnosticsContext diagnostics
    ) {
        OdDayPathSearchResult result;
        MATHFP_TRY(search_od_day_paths_by_origin_branch_and_bound(
              network
            , tasks
            , execution
            , params
            , search_cost
            , choice_config
            , assignment_period
            , admissibility_config
            , pruning_execution
            , complete_connection_dominance
            , [&](OriginDaySearchResult origin_result) -> mathfp::Expected<mathfp::Unit> {
                  result.origin_results.push_back(std::move(origin_result));
                  return mathfp::kUnit;
              }
            , diagnostics
        ));
        return result;
    }

    mathfp::Expected<OdDayConnectionSearchResult> search_od_day_connections_branch_and_bound(
          const PreprocessedNetwork& network
        , std::span<const SearchTask> tasks
        , SearchExecutionRequest     execution
        , const SearchParams&        params
        , const SearchCostContext&   search_cost
        , const ChoiceConfig&         choice_config
        , const AssignmentPeriodConfig& assignment_period
        , const ConnectionAdmissibilityConfig& admissibility_config
        , const SearchPruningExecutionPlan* pruning_execution
        , const CompleteConnectionDominanceConfig& complete_connection_dominance
        , SearchDiagnosticsContext diagnostics
    ) {
        return search_od_day_paths_branch_and_bound(
              network
            , tasks
            , execution
            , params
            , search_cost
            , choice_config
            , assignment_period
            , admissibility_config
            , pruning_execution
            , complete_connection_dominance
            , diagnostics
        );
    }

    mathfp::Expected<mathfp::Unit> search_od_day_connections_by_origin_branch_and_bound(
          const PreprocessedNetwork& network
        , std::span<const SearchTask> tasks
        , SearchExecutionRequest     execution
        , const SearchParams&        params
        , const SearchCostContext&   search_cost
        , const ChoiceConfig&         choice_config
        , const AssignmentPeriodConfig& assignment_period
        , const ConnectionAdmissibilityConfig& admissibility_config
        , const SearchPruningExecutionPlan* pruning_execution
        , const CompleteConnectionDominanceConfig& complete_connection_dominance
        , OdDayOriginResultSink      origin_sink
        , SearchDiagnosticsContext diagnostics
    ) {
        return search_od_day_paths_by_origin_branch_and_bound(
              network
            , tasks
            , execution
            , params
            , search_cost
            , choice_config
            , assignment_period
            , admissibility_config
            , pruning_execution
            , complete_connection_dominance
            , std::move(origin_sink)
            , diagnostics
        );
    }

    mathfp::Expected<ConnectionSearchResult> search_connections_branch_and_bound(
          const PreprocessedNetwork& network
        , std::span<const SearchTask> tasks
        , const SearchParams&        params
        , const SearchCostContext&   search_cost
        , const ChoiceConfig&         choice_config
        , const AssignmentPeriodConfig& assignment_period
        , const ConnectionAdmissibilityConfig& admissibility_config
        , const SearchPruningExecutionPlan* pruning_execution
        , const CompleteConnectionDominanceConfig& complete_connection_dominance
        , SearchDiagnosticsContext diagnostics
    ) {
        return search_connections_branch_and_bound(
              network
            , tasks
            , SearchExecutionRequest{
                  .config = make_interval_local_search_execution_config()
              }
            , params
            , search_cost
            , choice_config
            , assignment_period
            , admissibility_config
            , pruning_execution
            , complete_connection_dominance
            , diagnostics
        );
    }

    mathfp::Expected<ConnectionSearchResult> search_connections_branch_and_bound(
          const PreprocessedNetwork& network
        , std::span<const SearchTask> tasks
        , const SearchParams&        params
        , const SearchCostContext&   search_cost
        , const ChoiceConfig&         choice_config
        , const AssignmentPeriodConfig& assignment_period
        , const ConnectionAdmissibilityConfig& admissibility_config
        , const SearchPruningExecutionPlan* pruning_execution
        , SearchDiagnosticsContext diagnostics
    ) {
        return search_connections_branch_and_bound(
              network
            , tasks
            , params
            , search_cost
            , choice_config
            , assignment_period
            , admissibility_config
            , pruning_execution
            , CompleteConnectionDominanceConfig{}
            , diagnostics
        );
    }

    mathfp::Expected<ConnectionSearchResult> search_connections_branch_and_bound(
          const PreprocessedNetwork& network
        , std::span<const SearchTask> tasks
        , SearchExecutionMode        execution_mode
        , const SearchParams&        params
        , const SearchCostContext&   search_cost
        , const ChoiceConfig&         choice_config
        , const AssignmentPeriodConfig& assignment_period
        , const ConnectionAdmissibilityConfig& admissibility_config
        , const SearchPruningExecutionPlan* pruning_execution
        , const CompleteConnectionDominanceConfig& complete_connection_dominance
        , SearchDiagnosticsContext diagnostics
    ) {
        return search_connections_branch_and_bound(
              network
            , tasks
            , SearchExecutionRequest{
                  .config = make_search_execution_config(execution_mode)
              }
            , params
            , search_cost
            , choice_config
            , assignment_period
            , admissibility_config
            , pruning_execution
            , complete_connection_dominance
            , diagnostics
        );
    }

    mathfp::Expected<ConnectionSearchResult> search_connections_branch_and_bound(
          const PreprocessedNetwork& network
        , std::span<const SearchTask> tasks
        , SearchExecutionMode        execution_mode
        , const SearchParams&        params
        , const SearchCostContext&   search_cost
        , const ChoiceConfig&         choice_config
        , const AssignmentPeriodConfig& assignment_period
        , const ConnectionAdmissibilityConfig& admissibility_config
        , const SearchPruningExecutionPlan* pruning_execution
        , SearchDiagnosticsContext diagnostics
    ) {
        return search_connections_branch_and_bound(
              network
            , tasks
            , SearchExecutionRequest{
                  .config = make_search_execution_config(execution_mode)
              }
            , params
            , search_cost
            , choice_config
            , assignment_period
            , admissibility_config
            , pruning_execution
            , CompleteConnectionDominanceConfig{}
            , diagnostics
        );
    }

    mathfp::Expected<ConnectionSearchResult> search_connections_branch_and_bound(
          const PreprocessedNetwork& network
        , std::span<const SearchTask> tasks
        , double                     fare_scale
        , const SearchParams&        params
        , const ChoiceConfig&         choice_config
        , const AssignmentPeriodConfig& assignment_period
        , const ConnectionAdmissibilityConfig& admissibility_config
        , const SearchPruningExecutionPlan* pruning_execution
        , const CompleteConnectionDominanceConfig& complete_connection_dominance
        , SearchDiagnosticsContext diagnostics
    ) {
        MATHFP_TRY_LET(
              SearchCostContext
            , search_cost
            , make_base_search_cost_context(params.impedance, fare_scale)
        );
        return search_connections_branch_and_bound(
              network
            , tasks
            , params
            , search_cost
            , choice_config
            , assignment_period
            , admissibility_config
            , pruning_execution
            , complete_connection_dominance
            , diagnostics
        );
    }

    mathfp::Expected<ConnectionSearchResult> search_connections_branch_and_bound(
          const PreprocessedNetwork& network
        , std::span<const SearchTask> tasks
        , double                     fare_scale
        , const SearchParams&        params
        , const ChoiceConfig&         choice_config
        , const AssignmentPeriodConfig& assignment_period
        , const ConnectionAdmissibilityConfig& admissibility_config
        , const SearchPruningExecutionPlan* pruning_execution
        , SearchDiagnosticsContext diagnostics
    ) {
        return search_connections_branch_and_bound(
              network
            , tasks
            , fare_scale
            , params
            , choice_config
            , assignment_period
            , admissibility_config
            , pruning_execution
            , CompleteConnectionDominanceConfig{}
            , diagnostics
        );
    }

}  // namespace timetable::domain::assignment
