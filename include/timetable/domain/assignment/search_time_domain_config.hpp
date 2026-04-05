#pragma once

#include "timetable/domain/assignment/search_time_domain_builder.hpp"
#include "timetable/domain/assignment/search_time_domain_plan.hpp"

namespace timetable::domain::assignment {

    /**
     * @brief Mathematical request for demand-induced search-time pruning.
     *
     * This is the domain-level configuration that a future params file should
     * populate. It expresses what time-domain model is requested, not how the
     * current search engine happens to execute it.
     */
    struct SearchTimeDomainModelConfig final {
        SearchWindowMode        requested_mode{ SearchWindowMode::Global };
        SearchTimePaddingPolicy padding_policy{};
    };

    /**
     * @brief Internal runtime policy for the currently implemented search engine.
     *
     * This is intentionally separate from SearchTimeDomainModelConfig:
     * - model config belongs to the mathematical problem statement;
     * - runtime config belongs to staged implementation rollout.
     */
    struct SearchTimeDomainRuntimeConfig final {
        SearchArchitecture           architecture  { SearchArchitecture::OriginWideBranchAndBound };
        SearchTimeDomainRolloutStage rollout_stage { SearchTimeDomainRolloutStage::Disabled };
    };

    /**
     * @brief Full domain configuration for demand-induced search-time pruning.
     *
     * Parsing remains an outer-layer concern: data sources and future params
     * parsers will populate this type, but the type itself belongs to the
     * domain model.
     */
    struct SearchTimeDomainConfig final {
        SearchTimeDomainModelConfig   model{};
        SearchTimeDomainRuntimeConfig runtime{};
    };

}  // namespace timetable::domain::assignment
