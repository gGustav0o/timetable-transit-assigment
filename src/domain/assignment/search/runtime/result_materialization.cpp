#include "timetable/domain/assignment/search/runtime/result_materialization.hpp"

#include <mutex>
#include <string>
#include <utility>

#include <mathfp/core/unit.hpp>

namespace timetable::domain::assignment::runtime::detail {

    std::string format_batch_interval(
        const std::optional<IntervalId>& interval
    ) {
        if (!interval.has_value()) {
            return "period";
        }
        return std::to_string(interval->get());
    }

    std::string format_optional_size_limit(
        std::optional<std::size_t> limit
    ) {
        return limit.has_value()
            ? std::to_string(*limit)
            : std::string("unbounded");
    }

    std::size_t search_slot_connection_count(
        std::span<const SearchSlotResult> results
    ) noexcept {
        std::size_t total = 0;
        for (const auto& result : results) {
            total += result.connection_count;
        }
        return total;
    }

    ConnectionSearchResult materialize_demand_task_search_result(
          std::span<const SearchTask>   tasks
        , std::vector<SearchSlotResult> slot_results
    ) {
        ConnectionSearchResult result;
        result.task_results.reserve(tasks.size());
        for (const auto& task : tasks) {
            result.task_results.push_back(
                SearchTaskResult{
                      .task        = task
                    , .connections = {}
                }
            );
        }

        for (auto& slot_result : slot_results) {
            if (slot_result.slot.kind != SearchProjectionSlotKind::DemandTask
                || !slot_result.slot.result_index.has_value()) {
                continue;
            }
            result.task_results[*slot_result.slot.result_index].connections =
                std::move(slot_result.connections);
        }
        return result;
    }

    OriginDaySearchResult materialize_origin_day_search_result(
          ZoneId                        origin
        , std::vector<SearchSlotResult> slot_results
    ) {
        OriginDaySearchResult result{
              .origin       = origin
            , .pair_results = {}
        };
        result.pair_results.reserve(slot_results.size());
        for (auto& slot_result : slot_results) {
            if (slot_result.slot.kind != SearchProjectionSlotKind::OdDayPair) {
                continue;
            }
            result.pair_results.push_back(
                OdDayPairResult{
                      .origin       = slot_result.slot.origin
                    , .destination  = slot_result.slot.destination
                    , .alternatives = std::move(slot_result.day_path_alternatives)
                }
            );
        }
        return result;
    }

    mathfp::Expected<mathfp::Unit> CountOnlyAllZoneSearchResultSink::accept(
        std::vector<SearchSlotResult> slot_results
    ) {
        std::lock_guard lock{ mutex };
        for (auto& slot_result : slot_results) {
            if (slot_result.slot.kind != SearchProjectionSlotKind::CompletionTarget) {
                continue;
            }
            counts[slot_result.slot.origin][slot_result.slot.destination] =
                slot_result.connection_count;
        }
        return mathfp::kUnit;
    }

    AllZoneConnectionSearchResult CountOnlyAllZoneSearchResultSink::materialize() {
        std::lock_guard lock{ mutex };
        AllZoneConnectionSearchResult result;
        result.tree_results.reserve(counts.size());
        for (const auto& [origin, targets] : counts) {
            AllZoneTreeResult tree{
                  .origin = origin
                , .target_results = {}
            };
            tree.target_results.reserve(targets.size());
            for (const auto& [destination, connection_count] : targets) {
                tree.target_results.push_back(
                    AllZoneTargetResult{
                          .origin           = origin
                        , .destination      = destination
                        , .connection_count = connection_count
                        , .connections      = {}
                    }
                );
            }
            result.tree_results.push_back(std::move(tree));
        }
        return result;
    }

}  // namespace timetable::domain::assignment::runtime::detail
