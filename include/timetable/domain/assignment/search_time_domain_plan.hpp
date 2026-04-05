#pragma once

#include <cstdint>
#include <optional>

#include <mathfp/core/expected.hpp>

#include "timetable/domain/assignment/search_time_domain_execution.hpp"

namespace timetable::domain::assignment {

    /**
     * @brief Search engine architecture currently available in the project.
     *
     * At present the implementation builds one connection tree per origin.
     * This matters because demand-induced domains finer than origin granularity
     * may require adaptation before they become executable.
     */
    enum class SearchArchitecture : std::uint8_t {
        OriginWideBranchAndBound
    };

    /**
     * @brief Incremental rollout levels for demand-induced time-domain search.
     *
     * Disabled:
     * - search ignores time-domain pruning.
     *
     * GlobalStrict:
     * - only Global demand-induced domains are enabled.
     *
     * PerOriginStrict:
     * - Global and PerOrigin are enabled exactly.
     *
     * PerOdConservativeFallback:
     * - PerOd is accepted, but adapted conservatively to origin-wide execution.
     */
    enum class SearchTimeDomainRolloutStage : std::uint8_t {
          Disabled
        , GlobalStrict
        , PerOriginStrict
        , PerOdConservativeFallback
    };

    /**
     * @brief Architecture-aware execution plan for time-domain search.
     *
     * requested_mode is the mathematical mode requested by the caller.
     * execution_adaptation describes how the current search engine will consume
     * the resulting catalog. If enabled == false, search runs without time-domain
     * pruning.
     */
    struct SearchTimeDomainExecutionPlan final {
        SearchArchitecture                        architecture         { SearchArchitecture::OriginWideBranchAndBound };
        SearchTimeDomainRolloutStage              rollout_stage        { SearchTimeDomainRolloutStage::Disabled };
        SearchWindowMode                          requested_mode       { SearchWindowMode::Global };
        bool                                      enabled              { false };
        std::optional<SearchTimeDomainAdaptation> execution_adaptation {};
    };

    mathfp::Expected<SearchTimeDomainExecutionPlan> plan_search_time_domain_execution(
          SearchArchitecture           architecture
        , SearchTimeDomainRolloutStage rollout_stage
        , SearchWindowMode             requested_mode
    );

    /**
     * @brief Build an executable origin-wide time-domain object according to the rollout plan.
     *
     * This function is the orchestration bridge between:
     * - mathematical request (`requested_mode`)
     * - builder spec/validation
     * - execution adaptation for the current search architecture
     *
     * Intentionally, this is not a builder primitive. It is an architecture-
     * level planning function used by higher-level orchestration.
     *
     * If the plan is disabled, the result is std::nullopt.
     */
    mathfp::Expected<std::optional<SearchTimeDomainExecution>> prepare_search_time_domain_execution(
          const InputModel&               input
        , const SearchTimePaddingPolicy&  padding_policy
        , const SplitParams&              split
        , SearchArchitecture              architecture
        , SearchTimeDomainRolloutStage    rollout_stage
        , SearchWindowMode                requested_mode
    );

}  // namespace timetable::domain::assignment
