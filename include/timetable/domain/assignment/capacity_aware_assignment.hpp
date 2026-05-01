#pragma once

#include <array>
#include <cstdint>
#include <string_view>
#include <vector>

#include <mathfp/core/expected.hpp>
#include <mathfp/core/unit.hpp>

#include "timetable/enum_string.hpp"
#include "timetable/domain/assignment/capacity.hpp"
#include "timetable/domain/assignment/search/search.hpp"
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

    /**
     * @brief Top-level domain switch for capacity-aware assignment.
     *
     * capacity_aware_split_enabled means that vehicle journey item loads affect
     * split impedance through a fixed-point layer. capacity_aware_search_enabled
     * is reserved for the later search layer, where capacity costs must be
     * fixed exogenously for a search iteration to preserve dominance semantics.
     *
     * This config is intentionally separate from VehicleJourneyItemOverload:
     * overload assessment is a post-assignment report, while this model changes
     * behavioral assignment.
     */
    struct CapacityAwareAssignmentConfig final {
        bool                    capacity_aware_split_enabled{ false };
        bool                    capacity_aware_search_enabled{ false };
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
     * @brief Diagnostics of the capacity-aware split fixed-point loop.
     *
     * Diagnostics describe the behavioral split layer only. Post-assignment
     * overload assessment has its own status model.
     */
    struct CapacityAwareSplitDiagnostics final {
        bool         enabled{ false };
        std::int32_t iterations{ 0 };
        bool         converged{ false };
        double       max_load_delta{ 0.0 };
        double       max_relative_load_delta{ 0.0 };
    };

    /**
     * @brief Public diagnostics of the capacity-aware behavioral assignment layer.
     *
     * This is the compact, output-facing projection of the fixed-point split
     * diagnostics. used_factor is the factor that was behaviorally applied; it
     * is zero when capacity-aware split is disabled.
     */
    struct CapacityAwareAssignmentDiagnostics final {
        bool                  capacity_aware_enabled{ false };
        std::int32_t          iterations{ 0 };
        bool                  converged{ false };
        double                max_load_delta{ 0.0 };
        Dimless               used_factor{};
        CapacityPenaltyPolicy penalty_policy{ CapacityPenaltyPolicy::VolumeCapacityRatio };
    };

    [[nodiscard]] mathfp::Expected<mathfp::Unit> validate_capacity_penalty_policy(
        CapacityPenaltyPolicy policy
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
        , bool                    capacity_aware_search_enabled
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

    [[nodiscard]] mathfp::Expected<CapacityAwareAssignmentDiagnostics> make_capacity_aware_assignment_diagnostics(
          bool                            capacity_aware_enabled
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
