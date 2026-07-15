#include "timetable/domain/assignment/search/relations/branch_metrics.hpp"

#include <mathfp/core/try.hpp>

namespace timetable::domain::assignment {

    Time partial_journey_time(
        const SearchPartialMetrics& metrics
    ) noexcept {
        return Time{
            metrics.current_time->value() - metrics.departure->value()
        };
    }

    Time partial_walk_time(
        const SearchPartialMetrics& metrics
    ) noexcept {
        return metrics.access_time + metrics.transfer_walk_time + metrics.egress_time;
    }

    ConnectionImpedanceComponents partial_impedance_components(
        const SearchPartialMetrics& metrics
    ) noexcept {
        return ConnectionImpedanceComponents{
              .in_vehicle_time    = metrics.in_vehicle_time
            , .access_time        = metrics.access_time
            , .egress_time        = metrics.egress_time
            , .transfer_walk_time = metrics.transfer_walk_time
            , .transfer_wait_time = metrics.transfer_wait_time
            , .transfer_count     = metrics.transfers
            , .fare               = metrics.fare
        };
    }

    mathfp::Expected<SearchPruningMetrics> make_partial_pruning_metrics(
          const SearchPartialMetrics& metrics
        , const SearchCostContext&    search_cost
    ) {
        //tex:
        // Projection from incremental branch state to the paper comparison
        // vector. For the current prefix $$c_y$$:
        // $$JT(c_y)=ARR(c_y)-DEP(c_y),\qquad TT(c_y)=TWait(c_y)+TWalk(c_y).$$
        // The last coordinate is evaluated by SearchCostContext as $$IMP(c_y)$$.
        const auto journey_time = partial_journey_time(metrics);
        const auto cost_components = SearchCostComponents{
              .base = partial_impedance_components(metrics)
            , .capacity_exposure = metrics.capacity_exposure
        };
        MATHFP_TRY_LET(
              double
            , impedance
            , search_impedance(cost_components, search_cost)
        );

        return SearchPruningMetrics{
              .departure    = *metrics.departure
            , .arrival      = *metrics.current_time
            , .journey_time = journey_time
            , .walk_time    = partial_walk_time(metrics)
            , .transfers    = metrics.transfers
            , .fare         = metrics.fare
            , .impedance    = impedance
        };
    }

    mathfp::Expected<SearchPruningMetrics> make_partial_pruning_metrics(
          const SearchBranch&      branch
        , const SearchCostContext& search_cost
    ) {
        return make_partial_pruning_metrics(branch.metrics, search_cost);
    }

    mathfp::Expected<SearchPruningMetrics> make_day_path_pruning_metrics(
          const SearchPartialMetrics& metrics
        , const SearchCostContext&    search_cost
    ) {
        if (metrics.departure.has_value()
            && metrics.current_time.has_value()) {
            return make_partial_pruning_metrics(metrics, search_cost);
        }

        const auto elapsed = partial_walk_time(metrics);
        const auto cost_components = SearchCostComponents{
              .base = partial_impedance_components(metrics)
            , .capacity_exposure = metrics.capacity_exposure
        };
        MATHFP_TRY_LET(
              double
            , impedance
            , search_impedance(cost_components, search_cost)
        );

        return SearchPruningMetrics{
              .departure    = Time{ 0.0 }
            , .arrival      = elapsed
            , .journey_time = elapsed
            , .walk_time    = elapsed
            , .transfers    = metrics.transfers
            , .fare         = metrics.fare
            , .impedance    = impedance
        };
    }

    mathfp::Expected<SearchPruningMetrics> make_day_path_pruning_metrics(
          const SearchBranch&      branch
        , const SearchCostContext& search_cost
    ) {
        return make_day_path_pruning_metrics(branch.metrics, search_cost);
    }

}  // namespace timetable::domain::assignment
