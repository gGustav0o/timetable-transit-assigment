#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include <mathfp/core/expected.hpp>

#include "timetable/domain/model.hpp"
#include "timetable/domain/params.hpp"
#include "timetable/domain/assignment/assignment_period.hpp"
#include "timetable/domain/assignment/capacity_aware_assignment.hpp"
#include "timetable/domain/assignment/choice/choice.hpp"
#include "timetable/domain/assignment/connection_admissibility.hpp"
#include "timetable/domain/assignment/day_path.hpp"

namespace timetable::domain::assignment {

    enum class DemandShareAlternativeSource : std::uint8_t {
          TimedConnection
        , DayPath
    };

    struct ConnectionDemandShare final {
        ZoneId                       origin;
        ZoneId                       destination;
        IntervalId                   interval;
        DemandShareAlternativeSource source{ DemandShareAlternativeSource::TimedConnection };
        /*
         * In OD-day production source is DayPath for compatibility with the
         * structural post-layer, but the actual split alternative is the
         * concrete timed support stored in day_path_support.
         */
        DayPathSignature             day_path{};
        SearchConnection             connection;
        std::optional<DayPathSupportDescriptor> day_path_support{};
        double                       passengers{};
        double                       probability{};
        double                       independence{};
        double                       split_impedance{};
    };

    //tex:
    // A split share is the realized mass $$P_a(c)$$ for one demand interval
    // $$a$$ and one selected connection/support $$c$$:
    // $$passengers=P_a(c),\qquad probability=P_a(c)/DEM(a).$$
    // OD-day production may report `source=DayPath`, but the assigned support is
    // still a concrete timed connection stored in `day_path_support`.

    enum class UnassignedDemandReason : std::uint8_t {
          NoChosenAlternatives
        , NoIntervalAdmissibleSupport
    };

    [[nodiscard]] constexpr const char* to_string(
        UnassignedDemandReason reason
    ) noexcept {
        switch (reason) {
            case UnassignedDemandReason::NoChosenAlternatives:
                return "no_chosen_alternatives";
            case UnassignedDemandReason::NoIntervalAdmissibleSupport:
                return "no_interval_admissible_support";
        }
        return "unknown";
    }

    struct UnassignedDemand final {
        ZoneId                 origin;
        ZoneId                 destination;
        IntervalId             interval;
        double                 passengers{};
        UnassignedDemandReason reason{ UnassignedDemandReason::NoChosenAlternatives };
    };

    struct OdDaySplitCertificate final {
        ZoneId     origin;
        ZoneId     destination;
        IntervalId interval;
        std::size_t candidate_support_count{};
        std::size_t interval_admissible_support_count{};
        std::size_t interval_rejected_support_count{};
        std::size_t share_count{};
        double      demand_passengers{};
        double      assigned_passengers{};
        double      unassigned_passengers{};
        double      probability_sum{};
        std::optional<UnassignedDemandReason> unassigned_reason{};
    };

    //tex:
    // A connection split certificate is the compact runtime witness for one demand
    // interval $$a$$. It records $$|C(a)|$$ while timed supports are still
    // present in the split layer, then output can verify conservation without
    // reconstructing $$C(a)$$ after production memory compaction.

    struct DemandSplitResult final {
        std::vector<ConnectionDemandShare> shares{};
        std::vector<UnassignedDemand>      unassigned{};
        std::vector<OdDaySplitCertificate> od_day_split_certificates{};
    };

    struct OdDemandInterval final {
        ZoneId     origin;
        ZoneId     destination;
        IntervalId interval;
        double     passengers{};
    };

    struct OdDemandIntervals final {
        ZoneId                        origin;
        ZoneId                        destination;
        std::vector<OdDemandInterval> intervals{};
    };

    /**
     * @brief Result of the capacity-aware fixed-point split layer.
     *
     * split_result is the last demand split evaluated by the behavioral model.
     * split_loads is the direct half-open vehicle-item projection of that split.
     * load_state is the MSA-smoothed state used for convergence diagnostics and
     * as the exogenous load state of the next capacity-aware iteration.
     */
    struct CapacityAwareDemandSplitResult final {
        DemandSplitResult                split_result{};
        VehicleJourneyItemLoads          split_loads{};
        VehicleJourneyItemLoadState      load_state{};
        CapacityAwareSplitDiagnostics    diagnostics{};
    };

    struct OriginDayDemandLoadResult final {
        OriginDayPathChoiceResult alternatives{};
        DemandSplitResult     split_result{};
        ElementarySegmentLoads elementary_segment_loads{};
    };

    /**
     * @brief Split each demand entry over the chosen alternatives of its task.
     */
    mathfp::Expected<DemandSplitResult> split_demand_over_connections(
          const ConnectionChoiceResult& choice_result
        , const InputModel&           input
        , const SearchParams&         params
        , const DemandSegmentTimeConfig& demand_segment_time
    );

    [[nodiscard]] mathfp::Expected<std::vector<OdDemandIntervals>> build_od_demand_intervals(
        const InputModel& input
    );

    /**
     * @brief Split demand intervals over OD-day alternatives.
     *
     * This is the required formulation boundary: path alternatives are keyed by
     * OD for the whole service day, while demand rows remain interval-specific
     * and are applied only at split/load time. The production split follows the
     * connection-tree-level connection split: for each demand interval it distributes
     * demand over all interval-admissible timed supports retained under the
     * OD-day path identities.
     */
    mathfp::Expected<DemandSplitResult> split_demand_over_od_day_paths(
          const OdDayPathChoiceResult&       choice_result
        , const InputModel&                  input
        , const SearchParams&                params
        , const DemandSegmentTimeConfig&     demand_segment_time
        , const AssignmentPeriodConfig&      assignment_period
        , const ConnectionAdmissibilityConfig& admissibility_config
    );

    mathfp::Expected<DemandSplitResult> split_origin_demand_over_od_day_paths(
          const OriginDayPathChoiceResult&   choice_result
        , const InputModel&                  input
        , const SearchParams&                params
        , const DemandSegmentTimeConfig&     demand_segment_time
        , const AssignmentPeriodConfig&      assignment_period
        , const ConnectionAdmissibilityConfig& admissibility_config
    );

    mathfp::Expected<OriginDayDemandLoadResult> load_origin_day_path_demand(
          const OriginDaySearchResult&       search_result
        , const InputModel&                  input
        , const SearchParams&                params
        , const SearchCostContext&           search_cost
        , const ChoiceConfig&                choice_config
        , const DemandSegmentTimeConfig&     demand_segment_time
        , const AssignmentPeriodConfig&      assignment_period
        , const ConnectionAdmissibilityConfig& admissibility_config
    );

    mathfp::Expected<DemandSplitResult> split_demand_over_od_day_connections(
          const OdDayConnectionChoiceResult& choice_result
        , const InputModel&                  input
        , const SearchParams&                params
        , const DemandSegmentTimeConfig&     demand_segment_time
        , const AssignmentPeriodConfig&      assignment_period
        , const ConnectionAdmissibilityConfig& admissibility_config
    );

    mathfp::Expected<DemandSplitResult> split_origin_demand_over_od_day_connections(
          const OriginDayChoiceResult&       choice_result
        , const InputModel&                  input
        , const SearchParams&                params
        , const DemandSegmentTimeConfig&     demand_segment_time
        , const AssignmentPeriodConfig&      assignment_period
        , const ConnectionAdmissibilityConfig& admissibility_config
    );

    mathfp::Expected<OriginDayDemandLoadResult> load_origin_day_demand(
          const OriginDaySearchResult&       search_result
        , const InputModel&                  input
        , const SearchParams&                params
        , const SearchCostContext&           search_cost
        , const ChoiceConfig&                choice_config
        , const DemandSegmentTimeConfig&     demand_segment_time
        , const AssignmentPeriodConfig&      assignment_period
        , const ConnectionAdmissibilityConfig& admissibility_config
    );

    /**
     * @brief One capacity-aware split evaluation over a fixed load state.
     *
     * This function does not solve the endogenous capacity fixed point by
     * itself. It evaluates split probabilities with capacity costs derived from
     * an externally supplied vehicle journey item load state. The iterative
     * layer must call this function repeatedly and update the load state.
     */
    mathfp::Expected<DemandSplitResult> split_demand_over_connections_capacity_aware(
          const ConnectionChoiceResult& choice_result
        , const InputModel&             input
        , const SearchParams&           params
        , const DemandSegmentTimeConfig& demand_segment_time
        , const CapacityAwareAssignmentConfig& capacity_config
        , const VehicleJourneyItemLoadState& load_state
        , const VehicleJourneyItemCapacitySet& capacity_set
    );

    /**
     * @brief Solve the capacity-aware split fixed point over a fixed alternative set.
     *
     * The function keeps search/choice alternatives fixed and iterates only the
     * demand split/load state relation. Loads are updated by MSA:
     * L_{k+1} = (1 - alpha_k) L_k + alpha_k L_hat_k, alpha_k = 1 / k.
     */
    mathfp::Expected<CapacityAwareDemandSplitResult> iterate_capacity_aware_split(
          const ConnectionChoiceResult& choice_result
        , const InputModel&             input
        , const SearchParams&           params
        , const DemandSegmentTimeConfig& demand_segment_time
        , const CapacityAwareAssignmentConfig& capacity_config
        , const VehicleJourneyItemCapacitySet& capacity_set
    );

}  // namespace timetable::domain::assignment
