#pragma once

#include <array>
#include <cstdint>
#include <string_view>
#include <vector>

#include <mathfp/core/expected.hpp>
#include <mathfp/core/unit.hpp>

#include "timetable/enum_string.hpp"
#include "timetable/domain/assignment/capacity.hpp"
#include "timetable/domain/assignment/od_day_path_result.hpp"
#include "timetable/domain/assignment/search/connection.hpp"
#include "timetable/domain/scalars.hpp"

namespace timetable::domain::assignment {

    /**
     * @brief Penalty shape used by capacity-aware assignment.
     *
     * VolumeCapacityRatio uses the full v/c value. ExcessVolumeCapacityRatio
     * uses only max(0, v/c - 1). Both policies are pure scalar transforms; the
     * conversion to generalized time belongs to capacity exposure functions.
     */
    enum class CapacityPenaltyPolicy : std::uint8_t {
          VolumeCapacityRatio
        , ExcessVolumeCapacityRatio
    };

    inline constexpr std::array kCapacityPenaltyPolicyTokens{
          timetable::EnumStringEntry<CapacityPenaltyPolicy>{
              CapacityPenaltyPolicy::VolumeCapacityRatio, "volume_capacity_ratio"
          }
        , timetable::EnumStringEntry<CapacityPenaltyPolicy>{
              CapacityPenaltyPolicy::ExcessVolumeCapacityRatio, "excess_volume_capacity_ratio"
          }
    };

    [[nodiscard]] inline constexpr std::string_view to_string(
        CapacityPenaltyPolicy value
    ) noexcept {
        return timetable::enum_to_string(value, kCapacityPenaltyPolicyTokens);
    }

    /**
     * @brief Numerical controls for the fixed-point capacity-aware split layer.
     *
     * max_iterations bounds the demand-load fixed-point iteration. The two
     * tolerances are expressed in passenger units and relative load units,
     * respectively, and are interpreted by the future iteration algorithm.
     */
    struct CapacityIterationConfig final {
        std::int32_t max_iterations{ 50 };
        double       absolute_load_tolerance{ 1.0e-6 };
        double       relative_load_tolerance{ 1.0e-6 };
    };

    enum class CapacityAwareSearchMode : std::uint8_t {
          Disabled
        , StoredOnly
        , Enabled
    };

    inline constexpr std::array kCapacityAwareSearchModeTokens{
          timetable::EnumStringEntry<CapacityAwareSearchMode>{
              CapacityAwareSearchMode::Disabled, "disabled"
          }
        , timetable::EnumStringEntry<CapacityAwareSearchMode>{
              CapacityAwareSearchMode::StoredOnly, "stored_only"
          }
        , timetable::EnumStringEntry<CapacityAwareSearchMode>{
              CapacityAwareSearchMode::Enabled, "enabled"
          }
    };

    [[nodiscard]] inline constexpr std::string_view to_string(
        CapacityAwareSearchMode value
    ) noexcept {
        return timetable::enum_to_string(value, kCapacityAwareSearchModeTokens);
    }

    /**
     * @brief Top-level domain switch for capacity-aware assignment.
     *
     * capacity_aware_split_enabled means that vehicle journey item loads affect
     * split impedance through a fixed-point layer. search_mode controls whether
     * SearchImp.volCapRatioFactor is disabled, stored without behavioral use,
     * or applied by the external assignment iteration layer with a fixed load
     * state per search iteration.
     *
     * This config is intentionally separate from VehicleJourneyItemOverload:
     * overload assessment is a post-assignment report, while this model changes
     * behavioral assignment.
     */
    struct CapacityAwareAssignmentConfig final {
        bool                    capacity_aware_split_enabled{ false };
        CapacityAwareSearchMode search_mode{ CapacityAwareSearchMode::Disabled };
        CapacityPenaltyPolicy   penalty_policy{ CapacityPenaltyPolicy::VolumeCapacityRatio };
        CapacityIterationConfig iteration{};
    };

    /**
     * @brief Iteration load state used by capacity-aware split.
     *
     * This type is deliberately distinct from VehicleJourneyItemOverloadAssessment.
     * It is an endogenous state of the assignment fixed point, not a report of
     * final overload status.
     */
    struct VehicleJourneyItemLoadState final {
        std::vector<VehicleJourneyItemLoad> items{};
    };

    /**
     * @brief Capacity-induced generalized-time exposure of one alternative.
     *
     * The intended formula is sum_e ride_time_e * phi(load_e / capacity_e),
     * where e ranges over occupied vehicle journey items. Therefore the scalar
     * has time units and can be added to perceived journey time after applying
     * the configured dimensionless factor.
     */
    struct CapacityExposure final {
        Time equivalent_time{};
    };

    /**
     * @brief Diagnostics of a capacity-aware fixed-point loop.
     *
     * The same scalar diagnostics are used for split-only iteration over a
     * fixed alternative set and for the outer search-choice-split assignment
     * iteration. Post-assignment overload assessment has its own status model.
     */
    struct CapacityAwareSplitDiagnostics final {
        bool         enabled{ false };
        std::int32_t iterations{ 0 };
        bool         converged{ false };
        double       max_load_delta{ 0.0 };
        double       max_relative_load_delta{ 0.0 };
    };

    struct CapacityLoadStateDelta final {
        double max_absolute{};
        double max_relative{};
    };

    /**
     * @brief Public diagnostics of the capacity-aware behavioral assignment layer.
     *
     * This is the compact, output-facing projection of the active fixed-point
     * loop. iterations/converged describe the common assignment loop when
     * capacity-aware search is enabled, otherwise the split-only loop.
     */
    struct CapacityAwareAssignmentDiagnostics final {
        bool                  capacity_aware_enabled{ false };
        bool                  capacity_aware_search_enabled{ false };
        std::int32_t          iterations{ 0 };
        bool                  converged{ false };
        double                max_load_delta{ 0.0 };
        Dimless               used_factor{};
        CapacityPenaltyPolicy penalty_policy{ CapacityPenaltyPolicy::VolumeCapacityRatio };
    };

    [[nodiscard]] mathfp::Expected<mathfp::Unit> validate_capacity_penalty_policy(
        CapacityPenaltyPolicy policy
    );

    [[nodiscard]] mathfp::Expected<mathfp::Unit> validate_capacity_aware_search_mode(
        CapacityAwareSearchMode mode
    );

    [[nodiscard]] mathfp::Expected<mathfp::Unit> validate_capacity_iteration_config(
        const CapacityIterationConfig& config
    );

    [[nodiscard]] mathfp::Expected<mathfp::Unit> validate_capacity_aware_assignment_config(
        const CapacityAwareAssignmentConfig& config
    );

    [[nodiscard]] mathfp::Expected<mathfp::Unit> validate_vehicle_journey_item_load_state(
        const VehicleJourneyItemLoadState& state
    );

    [[nodiscard]] mathfp::Expected<mathfp::Unit> validate_capacity_exposure(
        CapacityExposure exposure
    );

    [[nodiscard]] mathfp::Expected<mathfp::Unit> validate_capacity_aware_split_diagnostics(
        const CapacityAwareSplitDiagnostics& diagnostics
    );

    [[nodiscard]] mathfp::Expected<mathfp::Unit> validate_capacity_aware_assignment_diagnostics(
        const CapacityAwareAssignmentDiagnostics& diagnostics
    );

    [[nodiscard]] mathfp::Expected<Dimless> capacity_ratio(
          double                            load
        , const VehicleJourneyItemCapacity& capacity
    );

    [[nodiscard]] mathfp::Expected<Dimless> capacity_ratio(
          const VehicleJourneyItemLoad&     load
        , const VehicleJourneyItemCapacity& capacity
    );

    [[nodiscard]] mathfp::Expected<Dimless> capacity_penalty(
          CapacityPenaltyPolicy policy
        , Dimless               ratio
    );

    [[nodiscard]] mathfp::Expected<CapacityExposure> connection_capacity_exposure(
          const Connection&                  connection
        , IntervalId                         interval
        , const VehicleJourneyItemLoadState& load_state
        , const VehicleJourneyItemCapacitySet& capacity_set
        , CapacityPenaltyPolicy             policy
    );

    [[nodiscard]] mathfp::Expected<CapacityExposure> connection_capacity_exposure(
          const SearchConnection&            connection
        , IntervalId                         interval
        , const VehicleJourneyItemLoadState& load_state
        , const VehicleJourneyItemCapacitySet& capacity_set
        , CapacityPenaltyPolicy             policy
    );

    [[nodiscard]] mathfp::Expected<CapacityExposure> day_path_support_capacity_exposure(
          const DayPathSupportDescriptor&    support
        , IntervalId                         interval
        , const VehicleJourneyItemLoadState& load_state
        , const VehicleJourneyItemCapacitySet& capacity_set
        , CapacityPenaltyPolicy             policy
    );

    [[nodiscard]] mathfp::Expected<double> capacity_adjusted_perceived_journey_time(
          double           base_perceived_journey_time
        , CapacityExposure exposure
        , Dimless          factor
    );

    [[nodiscard]] mathfp::Expected<double> capacity_adjusted_split_impedance(
          double           base_split_impedance
        , CapacityExposure exposure
        , Dimless          q_time
        , Dimless          perceived_journey_time_capacity_factor
    );

    [[nodiscard]] mathfp::Expected<CapacityIterationConfig> make_capacity_iteration_config(
          std::int32_t max_iterations
        , double       absolute_load_tolerance
        , double       relative_load_tolerance
    );

    [[nodiscard]] mathfp::Expected<CapacityAwareAssignmentConfig> make_capacity_aware_assignment_config(
          bool                    capacity_aware_split_enabled
        , CapacityAwareSearchMode search_mode
        , CapacityPenaltyPolicy   penalty_policy
        , CapacityIterationConfig iteration
    );

    [[nodiscard]] mathfp::Expected<VehicleJourneyItemLoadState> make_vehicle_journey_item_load_state(
        std::vector<VehicleJourneyItemLoad> items
    );

    [[nodiscard]] mathfp::Expected<VehicleJourneyItemLoadState> make_vehicle_journey_item_load_state(
        const VehicleJourneyItemLoads& loads
    );

    [[nodiscard]] mathfp::Expected<CapacityExposure> make_capacity_exposure(
        Time equivalent_time
    );

    [[nodiscard]] CapacityLoadStateDelta load_state_delta(
          const VehicleJourneyItemLoadState& previous
        , const VehicleJourneyItemLoadState& next
    );

    [[nodiscard]] mathfp::Expected<VehicleJourneyItemLoadState> msa_update_load_state(
          const VehicleJourneyItemLoadState& previous
        , const VehicleJourneyItemLoads&     candidate
        , double                             alpha
    );

    [[nodiscard]] bool capacity_iteration_converged(
          const CapacityIterationConfig& iteration
        , CapacityLoadStateDelta         delta
    ) noexcept;

    [[nodiscard]] mathfp::Expected<CapacityAwareAssignmentDiagnostics> make_capacity_aware_assignment_diagnostics(
          bool                            capacity_aware_enabled
        , bool                            capacity_aware_search_enabled
        , const CapacityAwareSplitDiagnostics& split_diagnostics
        , Dimless                         used_factor
        , CapacityPenaltyPolicy           penalty_policy
    );

    [[nodiscard]] CapacityAwareAssignmentDiagnostics make_capacity_aware_assignment_disabled_diagnostics(
        CapacityPenaltyPolicy penalty_policy = CapacityPenaltyPolicy::VolumeCapacityRatio
    );

    [[nodiscard]] CapacityAwareAssignmentConfig make_capacity_aware_assignment_disabled_config();

    [[nodiscard]] CapacityAwareSplitDiagnostics make_capacity_aware_split_disabled_diagnostics();

}  // namespace timetable::domain::assignment
