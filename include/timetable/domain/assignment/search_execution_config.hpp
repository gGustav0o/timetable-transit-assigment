#pragma once

#include <array>
#include <cstdint>
#include <cstddef>
#include <optional>
#include <string_view>

#include "timetable/enum_string.hpp"

namespace timetable::domain::assignment {

    /**
     * @brief User-facing calculation formulation.
     *
     * This is a launch-level preset. It deliberately sits above individual
     * search execution fields so params.txt can request the mathematical
     * formulation directly instead of spelling out an error-prone tuple of
     * origin/time/destination/projection scopes.
     *
     * TimedConnectionDiagnostics is the only supported contour for legacy
     * timed path enumeration. The required production formulation is
     * OdDayAssignment; DemandTaskAssignment is kept only as a rejected legacy
     * token for explicit configuration errors.
     */
    enum class AssignmentCalculationFormulation : std::uint8_t {
          DemandTaskAssignment
        , OdDayAssignment
        , AllZoneSearch
        , TimedConnectionDiagnostics
    };

    inline constexpr std::array kAssignmentCalculationFormulationTokens{
          timetable::EnumStringEntry<AssignmentCalculationFormulation>{
              AssignmentCalculationFormulation::DemandTaskAssignment,
              "demand_task_assignment"
          }
        , timetable::EnumStringEntry<AssignmentCalculationFormulation>{
              AssignmentCalculationFormulation::OdDayAssignment,
              "od_day_assignment"
          }
        , timetable::EnumStringEntry<AssignmentCalculationFormulation>{
              AssignmentCalculationFormulation::AllZoneSearch,
              "all_zone_search"
          }
        , timetable::EnumStringEntry<AssignmentCalculationFormulation>{
              AssignmentCalculationFormulation::TimedConnectionDiagnostics,
              "timed_connection_diagnostics"
          }
    };

    [[nodiscard]] inline constexpr std::string_view to_string(
        AssignmentCalculationFormulation value
    ) noexcept {
        return timetable::enum_to_string(value, kAssignmentCalculationFormulationTokens);
    }

    [[nodiscard]] inline constexpr std::optional<AssignmentCalculationFormulation>
    assignment_calculation_formulation_from_string(std::string_view token) noexcept {
        return timetable::enum_from_string(token, kAssignmentCalculationFormulationTokens);
    }

    /**
     * @brief High-level execution contract for timetable connection search.
     *
     * IntervalLocal is the faster interval-local path: search work is shared
     * only inside groups with the same origin and demand interval.
     *
     * OriginPeriod is the default architecture for origin-wide trees. The
     * application default is the required OD-day assignment contract: one
     * service-day tree per declared origin, retained by declared OD pairs.
     */
    enum class SearchExecutionMode : std::uint8_t {
          IntervalLocal
        , OriginPeriod
    };

    inline constexpr std::array kSearchExecutionModeTokens{
          timetable::EnumStringEntry<SearchExecutionMode>{
              SearchExecutionMode::IntervalLocal, "interval_local"
          }
        , timetable::EnumStringEntry<SearchExecutionMode>{
              SearchExecutionMode::OriginPeriod, "origin_period"
          }
    };

    [[nodiscard]] inline constexpr std::string_view to_string(
        SearchExecutionMode value
    ) noexcept {
        return timetable::enum_to_string(value, kSearchExecutionModeTokens);
    }

    [[nodiscard]] inline constexpr std::optional<SearchExecutionMode> search_execution_mode_from_string(
        std::string_view token
    ) noexcept {
        return timetable::enum_from_string(token, kSearchExecutionModeTokens);
    }

    enum class SearchOriginScope : std::uint8_t {
          ActiveDemandOrigins
        , DeclaredZones
    };

    inline constexpr std::array kSearchOriginScopeTokens{
          timetable::EnumStringEntry<SearchOriginScope>{
              SearchOriginScope::ActiveDemandOrigins, "active_demand_origins"
          }
        , timetable::EnumStringEntry<SearchOriginScope>{
              SearchOriginScope::DeclaredZones, "declared_zones"
          }
    };

    [[nodiscard]] inline constexpr std::string_view to_string(
        SearchOriginScope value
    ) noexcept {
        return timetable::enum_to_string(value, kSearchOriginScopeTokens);
    }

    [[nodiscard]] inline constexpr std::optional<SearchOriginScope> search_origin_scope_from_string(
        std::string_view token
    ) noexcept {
        return timetable::enum_from_string(token, kSearchOriginScopeTokens);
    }

    enum class SearchTimeDomainSource : std::uint8_t {
          DemandInduced
        , AssignmentPeriod
        , ServiceDay
    };

    inline constexpr std::array kSearchTimeDomainSourceTokens{
          timetable::EnumStringEntry<SearchTimeDomainSource>{
              SearchTimeDomainSource::DemandInduced, "demand_induced"
          }
        , timetable::EnumStringEntry<SearchTimeDomainSource>{
              SearchTimeDomainSource::AssignmentPeriod, "assignment_period"
          }
        , timetable::EnumStringEntry<SearchTimeDomainSource>{
              SearchTimeDomainSource::ServiceDay, "service_day"
          }
    };

    [[nodiscard]] inline constexpr std::string_view to_string(
        SearchTimeDomainSource value
    ) noexcept {
        return timetable::enum_to_string(value, kSearchTimeDomainSourceTokens);
    }

    [[nodiscard]] inline constexpr std::optional<SearchTimeDomainSource> search_time_domain_source_from_string(
        std::string_view token
    ) noexcept {
        return timetable::enum_from_string(token, kSearchTimeDomainSourceTokens);
    }

    enum class SearchDestinationScope : std::uint8_t {
          DemandDestinations
        , DeclaredZones
    };

    inline constexpr std::array kSearchDestinationScopeTokens{
          timetable::EnumStringEntry<SearchDestinationScope>{
              SearchDestinationScope::DemandDestinations, "demand_destinations"
          }
        , timetable::EnumStringEntry<SearchDestinationScope>{
              SearchDestinationScope::DeclaredZones, "declared_zones"
          }
    };

    [[nodiscard]] inline constexpr std::string_view to_string(
        SearchDestinationScope value
    ) noexcept {
        return timetable::enum_to_string(value, kSearchDestinationScopeTokens);
    }

    [[nodiscard]] inline constexpr std::optional<SearchDestinationScope> search_destination_scope_from_string(
        std::string_view token
    ) noexcept {
        return timetable::enum_from_string(token, kSearchDestinationScopeTokens);
    }

    /**
     * @brief Result materialization contract for a search tree.
     *
     * DemandTasks is the legacy OD-interval assignment projection: complete
     * connections are retained in task-local result slots induced by positive
     * OD demand rows.
     *
     * OdDayPairs is the required assignment projection for the current
     * formulation: one service-day tree per declared origin, all declared
     * destinations as completion targets, and complete alternatives retained by
     * OD pair rather than by demand interval.
     *
     * CompletionTargets is the all-zone diagnostic projection: complete
     * connections are retained for the tree completion targets themselves and
     * may be materialized as counts only. This contract requires a distinct
     * all-zone result materializer; it must not be squeezed into SearchTaskResult.
     */
    enum class SearchResultProjection : std::uint8_t {
          DemandTasks
        , OdDayPairs
        , CompletionTargets
    };

    inline constexpr std::array kSearchResultProjectionTokens{
          timetable::EnumStringEntry<SearchResultProjection>{
              SearchResultProjection::DemandTasks, "demand_tasks"
          }
        , timetable::EnumStringEntry<SearchResultProjection>{
              SearchResultProjection::OdDayPairs, "od_day_pairs"
          }
        , timetable::EnumStringEntry<SearchResultProjection>{
              SearchResultProjection::CompletionTargets, "completion_targets"
          }
    };

    [[nodiscard]] inline constexpr std::string_view to_string(
        SearchResultProjection value
    ) noexcept {
        return timetable::enum_to_string(value, kSearchResultProjectionTokens);
    }

    [[nodiscard]] inline constexpr std::optional<SearchResultProjection>
    search_result_projection_from_string(std::string_view token) noexcept {
        return timetable::enum_from_string(token, kSearchResultProjectionTokens);
    }

    /**
     * @brief Scope of partial-branch retention inside one search tree.
     *
     * ProjectionSlotLocal is the conservative legacy implementation: partial
     * prefixes are retained separately for every result projection slot.
     *
     * TreeGlobal is the article-like branch-and-bound implementation:
     * partial-prefix relevance is checked once per tree state, while complete
     * alternatives remain retained per result projection slot.
     */
    enum class SearchPartialRetentionScope : std::uint8_t {
          ProjectionSlotLocal
        , TreeGlobal
    };

    inline constexpr std::array kSearchPartialRetentionScopeTokens{
          timetable::EnumStringEntry<SearchPartialRetentionScope>{
              SearchPartialRetentionScope::ProjectionSlotLocal,
              "projection_slot_local"
          }
        , timetable::EnumStringEntry<SearchPartialRetentionScope>{
              SearchPartialRetentionScope::TreeGlobal, "tree_global"
          }
    };

    [[nodiscard]] inline constexpr std::string_view to_string(
        SearchPartialRetentionScope value
    ) noexcept {
        return timetable::enum_to_string(value, kSearchPartialRetentionScopeTokens);
    }

    [[nodiscard]] inline constexpr std::optional<SearchPartialRetentionScope>
    search_partial_retention_scope_from_string(std::string_view token) noexcept {
        return timetable::enum_from_string(token, kSearchPartialRetentionScopeTokens);
    }

    /**
     * @brief Domain-level search execution configuration.
     *
     * The four scope fields define the mathematical tree contract:
     *
     * - mode selects whether search is interval-local or origin-period.
     * - origin_scope defines the set of origins for which tree jobs exist.
     * - time_domain_source defines the first-boarding time domain.
     * - destination_scope defines the completion targets of each tree.
     *
     * result_projection is intentionally separate from destination_scope.
     * Completion targets answer "where may a tree finish"; result projection
     * answers "where are complete alternatives retained". This separation is
     * required because OD assignment and all-zone/VISUM-like enumeration share
     * the same branch-and-bound tree but materialize different mathematical
     * outputs.
     *
     * partial_retention_scope is separate from both fields: it defines whether
     * partial-branch relevance is stored globally for the tree, as in the
     * article, or independently for each projection slot, as in the conservative
     * legacy implementation.
     *
     * validate_phase_invariants is a diagnostic switch. It enables expensive
     * per-branch checks of the phase automaton invariants and must not change
     * the mathematical search contract or the set of admissible branches.
     *
     * DemandTasks contract:
     * - origins: configured by origin_scope;
     * - time horizon: configured by time_domain_source;
     * - completion targets: configured by destination_scope;
     * - projection slots: positive OD-demand tasks;
     * - result type: ConnectionSearchResult task_results.
     *
     * OdDayPairs contract:
     * - origins: declared zones, one service-day tree per origin;
     * - time horizon: the whole service day;
     * - completion targets: declared zones;
     * - projection slots: ordered OD pairs (origin, destination);
     * - result type: OdDayPathSearchResult, kept separate from both
     *   demand-task assignment and all-zone count diagnostics.
     *
     * CompletionTargets contract:
     * - origins: configured by origin_scope, typically DeclaredZones;
     * - time horizon: configured by time_domain_source, typically ServiceDay;
     * - completion targets: configured by destination_scope, typically DeclaredZones;
     * - projection slots: completion targets themselves;
     * - result type: a separate all-zone tree result materializer.
     */
    struct SearchExecutionConfig final {
        static constexpr std::size_t kDefaultMaxParallelBatches = 6;
        static constexpr std::size_t kDefaultMaxOdDayLabelRepresentativesPerState = 8;

        AssignmentCalculationFormulation formulation{
            AssignmentCalculationFormulation::OdDayAssignment
        };
        bool                     diagnostic_mode{ false };
        SearchExecutionMode      mode{ SearchExecutionMode::OriginPeriod };
        SearchOriginScope        origin_scope{ SearchOriginScope::DeclaredZones };
        SearchTimeDomainSource   time_domain_source{ SearchTimeDomainSource::ServiceDay };
        SearchDestinationScope   destination_scope{ SearchDestinationScope::DeclaredZones };
        SearchResultProjection   result_projection{ SearchResultProjection::OdDayPairs };
        SearchPartialRetentionScope partial_retention_scope{
            SearchPartialRetentionScope::TreeGlobal
        };
        std::size_t max_parallel_batches{ kDefaultMaxParallelBatches };
        std::size_t max_od_day_label_representatives_per_state{
            kDefaultMaxOdDayLabelRepresentativesPerState
        };
        bool validate_phase_invariants{ false };
        bool log_projection_details{ false };
    };

    [[nodiscard]] inline constexpr SearchExecutionConfig make_default_demand_assignment_search_execution_config(
    ) noexcept {
        return SearchExecutionConfig{
              .formulation        = AssignmentCalculationFormulation::DemandTaskAssignment
            , .diagnostic_mode    = false
            , .mode               = SearchExecutionMode::OriginPeriod
            , .origin_scope       = SearchOriginScope::ActiveDemandOrigins
            , .time_domain_source = SearchTimeDomainSource::ServiceDay
            , .destination_scope  = SearchDestinationScope::DemandDestinations
            , .result_projection  = SearchResultProjection::DemandTasks
            , .partial_retention_scope = SearchPartialRetentionScope::ProjectionSlotLocal
            , .max_parallel_batches = SearchExecutionConfig::kDefaultMaxParallelBatches
            , .validate_phase_invariants = false
        };
    }

    [[nodiscard]] inline constexpr SearchExecutionConfig make_interval_local_search_execution_config(
    ) noexcept {
        return SearchExecutionConfig{
              .formulation        = AssignmentCalculationFormulation::DemandTaskAssignment
            , .diagnostic_mode    = false
            , .mode               = SearchExecutionMode::IntervalLocal
            , .origin_scope       = SearchOriginScope::ActiveDemandOrigins
            , .time_domain_source = SearchTimeDomainSource::DemandInduced
            , .destination_scope  = SearchDestinationScope::DemandDestinations
            , .result_projection  = SearchResultProjection::DemandTasks
            , .partial_retention_scope = SearchPartialRetentionScope::ProjectionSlotLocal
            , .max_parallel_batches = SearchExecutionConfig::kDefaultMaxParallelBatches
            , .validate_phase_invariants = false
        };
    }

    [[nodiscard]] inline constexpr SearchExecutionConfig make_declared_origin_period_demand_task_search_execution_config(
    ) noexcept {
        return SearchExecutionConfig{
              .formulation        = AssignmentCalculationFormulation::DemandTaskAssignment
            , .diagnostic_mode    = false
            , .mode               = SearchExecutionMode::OriginPeriod
            , .origin_scope       = SearchOriginScope::DeclaredZones
            , .time_domain_source = SearchTimeDomainSource::ServiceDay
            , .destination_scope  = SearchDestinationScope::DeclaredZones
            , .result_projection  = SearchResultProjection::DemandTasks
            , .partial_retention_scope = SearchPartialRetentionScope::ProjectionSlotLocal
            , .max_parallel_batches = SearchExecutionConfig::kDefaultMaxParallelBatches
            , .validate_phase_invariants = false
        };
    }

    [[nodiscard]] inline constexpr SearchExecutionConfig make_active_demand_origin_period_search_execution_config(
    ) noexcept {
        return SearchExecutionConfig{
              .formulation        = AssignmentCalculationFormulation::DemandTaskAssignment
            , .diagnostic_mode    = false
            , .mode               = SearchExecutionMode::OriginPeriod
            , .origin_scope       = SearchOriginScope::ActiveDemandOrigins
            , .time_domain_source = SearchTimeDomainSource::DemandInduced
            , .destination_scope  = SearchDestinationScope::DemandDestinations
            , .result_projection  = SearchResultProjection::DemandTasks
            , .partial_retention_scope = SearchPartialRetentionScope::ProjectionSlotLocal
            , .max_parallel_batches = SearchExecutionConfig::kDefaultMaxParallelBatches
            , .validate_phase_invariants = false
        };
    }

    [[nodiscard]] inline constexpr SearchExecutionConfig make_all_zone_origin_period_search_execution_config(
    ) noexcept {
        return SearchExecutionConfig{
              .formulation        = AssignmentCalculationFormulation::AllZoneSearch
            , .diagnostic_mode    = true
            , .mode               = SearchExecutionMode::OriginPeriod
            , .origin_scope       = SearchOriginScope::DeclaredZones
            , .time_domain_source = SearchTimeDomainSource::ServiceDay
            , .destination_scope  = SearchDestinationScope::DeclaredZones
            , .result_projection  = SearchResultProjection::CompletionTargets
            , .partial_retention_scope = SearchPartialRetentionScope::TreeGlobal
            , .max_parallel_batches = SearchExecutionConfig::kDefaultMaxParallelBatches
            , .validate_phase_invariants = false
        };
    }

    [[nodiscard]] inline constexpr SearchExecutionConfig make_od_day_assignment_search_execution_config(
    ) noexcept {
        return SearchExecutionConfig{
              .formulation        = AssignmentCalculationFormulation::OdDayAssignment
            , .diagnostic_mode    = false
            , .mode               = SearchExecutionMode::OriginPeriod
            , .origin_scope       = SearchOriginScope::DeclaredZones
            , .time_domain_source = SearchTimeDomainSource::ServiceDay
            , .destination_scope  = SearchDestinationScope::DeclaredZones
            , .result_projection  = SearchResultProjection::OdDayPairs
            , .partial_retention_scope = SearchPartialRetentionScope::TreeGlobal
            , .max_parallel_batches = SearchExecutionConfig::kDefaultMaxParallelBatches
            , .validate_phase_invariants = false
        };
    }

    [[nodiscard]] inline constexpr SearchExecutionConfig make_timed_connection_diagnostics_search_execution_config(
    ) noexcept {
        return SearchExecutionConfig{
              .formulation        = AssignmentCalculationFormulation::TimedConnectionDiagnostics
            , .diagnostic_mode    = true
            , .mode               = SearchExecutionMode::OriginPeriod
            , .origin_scope       = SearchOriginScope::ActiveDemandOrigins
            , .time_domain_source = SearchTimeDomainSource::ServiceDay
            , .destination_scope  = SearchDestinationScope::DemandDestinations
            , .result_projection  = SearchResultProjection::DemandTasks
            , .partial_retention_scope = SearchPartialRetentionScope::ProjectionSlotLocal
            , .max_parallel_batches = SearchExecutionConfig::kDefaultMaxParallelBatches
            , .validate_phase_invariants = false
        };
    }

    [[nodiscard]] inline constexpr SearchExecutionConfig make_default_search_execution_config(
    ) noexcept {
        return make_od_day_assignment_search_execution_config();
    }

    [[nodiscard]] inline constexpr SearchExecutionConfig make_declared_origin_period_search_execution_config(
    ) noexcept {
        return make_all_zone_origin_period_search_execution_config();
    }

    [[nodiscard]] inline constexpr SearchExecutionConfig make_search_execution_config(
        AssignmentCalculationFormulation formulation
    ) noexcept {
        switch (formulation) {
            case AssignmentCalculationFormulation::DemandTaskAssignment:
                return make_default_demand_assignment_search_execution_config();

            case AssignmentCalculationFormulation::OdDayAssignment:
                return make_od_day_assignment_search_execution_config();

            case AssignmentCalculationFormulation::AllZoneSearch:
                return make_all_zone_origin_period_search_execution_config();

            case AssignmentCalculationFormulation::TimedConnectionDiagnostics:
                return make_timed_connection_diagnostics_search_execution_config();
        }
        return make_default_search_execution_config();
    }

    [[nodiscard]] inline constexpr SearchExecutionConfig make_search_execution_config(
        SearchExecutionMode mode
    ) noexcept {
        switch (mode) {
            case SearchExecutionMode::IntervalLocal:
                return make_interval_local_search_execution_config();

            case SearchExecutionMode::OriginPeriod:
                return make_declared_origin_period_search_execution_config();
        }
        return make_declared_origin_period_search_execution_config();
    }

    [[nodiscard]] inline constexpr bool is_required_od_day_assignment_profile(
        const SearchExecutionConfig& config
    ) noexcept {
        return config.formulation == AssignmentCalculationFormulation::OdDayAssignment
            && !config.diagnostic_mode
            && config.mode == SearchExecutionMode::OriginPeriod
            && config.origin_scope == SearchOriginScope::DeclaredZones
            && config.time_domain_source == SearchTimeDomainSource::ServiceDay
            && config.destination_scope == SearchDestinationScope::DeclaredZones
            && config.result_projection == SearchResultProjection::OdDayPairs
            && config.partial_retention_scope == SearchPartialRetentionScope::TreeGlobal
            && config.max_parallel_batches > 0u
            && config.max_od_day_label_representatives_per_state > 0u;
    }

    [[nodiscard]] inline constexpr bool is_timed_connection_diagnostics_profile(
        const SearchExecutionConfig& config
    ) noexcept {
        return config.formulation == AssignmentCalculationFormulation::TimedConnectionDiagnostics
            && config.diagnostic_mode
            && config.mode == SearchExecutionMode::OriginPeriod
            && config.origin_scope == SearchOriginScope::ActiveDemandOrigins
            && config.time_domain_source == SearchTimeDomainSource::ServiceDay
            && config.destination_scope == SearchDestinationScope::DemandDestinations
            && config.result_projection == SearchResultProjection::DemandTasks
            && config.partial_retention_scope == SearchPartialRetentionScope::ProjectionSlotLocal
            && config.max_parallel_batches > 0u;
    }

    [[nodiscard]] inline constexpr bool is_all_zone_search_diagnostics_profile(
        const SearchExecutionConfig& config
    ) noexcept {
        return config.formulation == AssignmentCalculationFormulation::AllZoneSearch
            && config.diagnostic_mode
            && config.mode == SearchExecutionMode::OriginPeriod
            && config.origin_scope == SearchOriginScope::DeclaredZones
            && config.time_domain_source == SearchTimeDomainSource::ServiceDay
            && config.destination_scope == SearchDestinationScope::DeclaredZones
            && config.result_projection == SearchResultProjection::CompletionTargets
            && config.partial_retention_scope == SearchPartialRetentionScope::TreeGlobal
            && config.max_parallel_batches > 0u;
    }

    [[nodiscard]] inline constexpr bool is_demand_task_assignment_profile(
        const SearchExecutionConfig& config
    ) noexcept {
        (void)config;
        return false;
    }

}  // namespace timetable::domain::assignment
