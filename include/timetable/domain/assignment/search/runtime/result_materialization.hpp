#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include <mathfp/core/expected.hpp>
#include <mathfp/core/unit.hpp>

#include "timetable/domain/assignment/od_day_path_result.hpp"
#include "timetable/domain/assignment/search/all_zone_result.hpp"
#include "timetable/domain/assignment/search/demand.hpp"
#include "timetable/domain/assignment/search/projection.hpp"
#include "timetable/domain/endpoints.hpp"
#include "timetable/domain/model.hpp"

namespace timetable::domain::assignment::runtime::detail {

    enum class OdDaySearchComputationContract : std::uint8_t {
          IncrementalDayPathRetention
        , StructuralEdgeExpansion
        , PaperConnectionSegmentTree
    };

    inline constexpr std::size_t kTaskProgressStep = 10;

    [[nodiscard]] constexpr const char* to_log_token(
        OdDaySearchComputationContract contract
    ) noexcept {
        switch (contract) {
            case OdDaySearchComputationContract::IncrementalDayPathRetention:
                return "incremental_day_path_retention";
            case OdDaySearchComputationContract::StructuralEdgeExpansion:
                return "structural_edge_expansion";
            case OdDaySearchComputationContract::PaperConnectionSegmentTree:
                return "paper_connection_segment_tree";
        }
        return "unknown";
    }

    [[nodiscard]] std::string format_batch_interval(
        const std::optional<IntervalId>& interval
    );

    [[nodiscard]] std::string format_optional_size_limit(
        std::optional<std::size_t> limit
    );

    [[nodiscard]] std::size_t search_slot_connection_count(
        std::span<const SearchSlotResult> results
    ) noexcept;

    [[nodiscard]] ConnectionSearchResult materialize_demand_task_search_result(
          std::span<const SearchTask>   tasks
        , std::vector<SearchSlotResult> slot_results
    );

    [[nodiscard]] OriginDaySearchResult materialize_origin_day_search_result(
          ZoneId                        origin
        , std::vector<SearchSlotResult> slot_results
    );

    struct CountOnlyAllZoneSearchResultSink final {
        std::mutex mutex{};
        std::map<ZoneId, std::map<ZoneId, std::size_t>> counts{};

        mathfp::Expected<mathfp::Unit> accept(
            std::vector<SearchSlotResult> slot_results
        );

        [[nodiscard]] AllZoneConnectionSearchResult materialize();
    };

}  // namespace timetable::domain::assignment::runtime::detail
