#pragma once

#include <cstdint>
#include <cstddef>
#include <optional>
#include <string>

#include "timetable/domain/assignment/search_time_domain_config.hpp"
#include "timetable/domain/assignment/search_time_domain_execution.hpp"

namespace timetable::domain::assignment {

    struct SearchTimeDomainSummary final {
        std::size_t                     window_count {};
        bool                            empty        { true };
        std::optional<SearchTimeWindow> bounds       {};
    };

    struct SearchTimeDomainCatalogSummary final {
        SearchWindowMode                mode                { SearchWindowMode::Global };
        SearchTimePadding               padding             {};
        std::size_t                     global_window_count {};
        std::size_t                     origin_slice_count  {};
        std::size_t                     od_slice_count      {};
        std::size_t                     total_window_count  {};
        std::optional<SearchTimeWindow> overall_bounds      {};
    };

    struct SearchTimeDomainExecutionSummary final {
        SearchWindowMode                source_mode        { SearchWindowMode::Global };
        SearchTimeDomainAdaptation      adaptation         { SearchTimeDomainAdaptation::Strict };
        bool                            has_global_domain  {};
        std::size_t                     origin_slice_count {};
        std::size_t                     total_window_count {};
        std::optional<SearchTimeWindow> overall_bounds     {};
    };

    struct SearchTimeDomainConfigSummary final {
        SearchWindowMode                requested_mode  { SearchWindowMode::Global };
        SearchArchitecture              architecture    { SearchArchitecture::OriginWideBranchAndBound };
        SearchTimeDomainRolloutStage    rollout_stage   { SearchTimeDomainRolloutStage::Disabled };
        bool                            strict_policy   {};
        bool                            fallback_policy {};
    };

    [[nodiscard]] SearchTimeDomainSummary summarize(
        const SearchTimeDomain& domain
    ) noexcept;

    [[nodiscard]] SearchTimeDomainCatalogSummary summarize(
        const SearchTimeDomainCatalog& catalog
    ) noexcept;

    [[nodiscard]] SearchTimeDomainExecutionSummary summarize(
        const SearchTimeDomainExecution& execution
    ) noexcept;

    [[nodiscard]] SearchTimeDomainConfigSummary summarize(
        const SearchTimeDomainConfig& config
    ) noexcept;

    [[nodiscard]] std::string format_search_time_domain_summary(
        const SearchTimeDomainSummary& summary
    );

    [[nodiscard]] std::string format_search_time_domain_catalog_summary(
        const SearchTimeDomainCatalogSummary& summary
    );

    [[nodiscard]] std::string format_search_time_domain_execution_summary(
        const SearchTimeDomainExecutionSummary& summary
    );

    [[nodiscard]] std::string format_search_time_domain_config_summary(
        const SearchTimeDomainConfigSummary& summary
    );

}  // namespace timetable::domain::assignment
