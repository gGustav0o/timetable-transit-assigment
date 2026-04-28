#include "timetable/domain/assignment/complete_connection_retention.hpp"

#include <algorithm>
#include <limits>
#include <utility>

#include <mathfp/types/units.hpp>

#include "timetable/domain/impedance.hpp"

namespace timetable::domain::assignment {
    namespace {

        [[nodiscard]] double complete_connection_impedance(
              const ConnectionMetrics& connection_metrics
            , const SearchParams&      params
            , double                   fare_scale
        ) noexcept {
            return connection_impedance_value(
                  ConnectionImpedanceComponents{
                      .in_vehicle_time    = connection_metrics.in_vehicle_time
                    , .access_time        = connection_metrics.access_time
                    , .egress_time        = connection_metrics.egress_time
                    , .transfer_walk_time = connection_metrics.transfer_walk_time
                    , .transfer_wait_time = connection_metrics.transfer_wait_time
                    , .transfer_count     = connection_metrics.transfer_count
                    , .fare               = connection_metrics.fare
                  }
                , params.impedance
                , fare_scale
            );
        }

        [[nodiscard]] CompleteConnectionAlternative make_complete_connection_alternative(
              SearchConnection    connection
            , const SearchParams& params
            , double              fare_scale
        ) {
            const auto metrics = complete_connection_metrics(connection, params, fare_scale);
            return CompleteConnectionAlternative{
                  .connection = std::move(connection)
                , .metrics    = metrics
            };
        }

        [[nodiscard]] bool same_connection_trace(
              const SearchConnection& lhs
            , const SearchConnection& rhs
        ) {
            return connection_segment_trace(lhs) == connection_segment_trace(rhs);
        }

        [[nodiscard]] CompleteConnectionMetricSummary summarize_complete_connection_metrics(
            const std::vector<CompleteConnectionAlternative>& alternatives
        ) noexcept {
            CompleteConnectionMetricSummary summary{
                  .min_impedance    = std::numeric_limits<double>::infinity()
                , .min_journey_time = std::numeric_limits<double>::infinity()
                , .min_transfers    = std::numeric_limits<double>::infinity()
                , .empty            = alternatives.empty()
            };
            for (const auto& alternative : alternatives) {
                summary.min_impedance = std::min(
                      summary.min_impedance
                    , alternative.metrics.impedance
                );
                summary.min_journey_time = std::min(
                      summary.min_journey_time
                    , alternative.metrics.journey_time.value()
                );
                summary.min_transfers = std::min(
                      summary.min_transfers
                    , static_cast<double>(alternative.metrics.transfers.get())
                );
            }
            return summary;
        }

        [[nodiscard]] bool complete_connection_order_less(
              const CompleteConnectionAlternative& lhs
            , const CompleteConnectionAlternative& rhs
        ) {
            if (lhs.metrics.departure != rhs.metrics.departure) {
                return lhs.metrics.departure.value() < rhs.metrics.departure.value();
            }
            if (lhs.metrics.arrival != rhs.metrics.arrival) {
                return lhs.metrics.arrival.value() < rhs.metrics.arrival.value();
            }
            if (lhs.metrics.impedance != rhs.metrics.impedance) {
                return lhs.metrics.impedance < rhs.metrics.impedance;
            }
            if (lhs.metrics.transfers != rhs.metrics.transfers) {
                return lhs.metrics.transfers.get() < rhs.metrics.transfers.get();
            }
            return connection_segment_trace(lhs.connection)
                 < connection_segment_trace(rhs.connection);
        }

        [[nodiscard]] std::vector<SearchConnection> materialize_sorted_connections(
            std::vector<CompleteConnectionAlternative> alternatives
        ) {
            std::sort(
                  alternatives.begin()
                , alternatives.end()
                , complete_connection_order_less
            );

            std::vector<SearchConnection> connections;
            connections.reserve(alternatives.size());
            for (auto& alternative : alternatives) {
                connections.push_back(std::move(alternative.connection));
            }
            return connections;
        }

    }  // namespace

    CompleteConnectionMetrics complete_connection_metrics(
          const SearchConnection& connection
        , const SearchParams&     params
        , double                  fare_scale
    ) {
        const auto connection_metrics = metrics_of(connection);
        return CompleteConnectionMetrics{
              .departure   = connection_metrics.departure_time
            , .arrival     = connection_metrics.arrival_time
            , .journey_time = connection_metrics.journey_time
            , .transfers    = connection_metrics.transfer_count
            , .impedance    = complete_connection_impedance(
                  connection_metrics
                , params
                , fare_scale
              )
        };
    }

    bool complete_connection_dominates(
          const CompleteConnectionMetrics& lhs
        , const CompleteConnectionMetrics& rhs
    ) noexcept {
        const auto no_worse =
               lhs.departure.value() >= rhs.departure.value()
            && lhs.arrival  .value() <= rhs.arrival  .value()
            && lhs.impedance          <= rhs.impedance
            && lhs.transfers.get()    <= rhs.transfers.get();

        const auto strictly_better =
               lhs.departure.value() > rhs.departure.value()
            || lhs.arrival  .value() < rhs.arrival  .value()
            || lhs.impedance          < rhs.impedance
            || lhs.transfers.get()    < rhs.transfers.get();

        return no_worse && strictly_better;
    }

    bool within_complete_connection_tolerances(
          const CompleteConnectionMetrics&       metrics
        , const CompleteConnectionMetricSummary& summary
        , const ChoiceTolerances&                tolerances
    ) noexcept {
        if (summary.empty) {
            return false;
        }
        return
               metrics.impedance
            <= mathfp::units::as_dimless(tolerances.imp_mult)
             * summary.min_impedance
             + mathfp::units::as_dimless(tolerances.imp_add)

            && metrics.journey_time.value()
            <= mathfp::units::as_dimless(tolerances.jt_mult)
             * summary.min_journey_time
             + mathfp::units::as_dimless(tolerances.jt_add)

            && static_cast<double>(metrics.transfers.get())
            <= mathfp::units::as_dimless(tolerances.nt_mult)
             * summary.min_transfers
             + mathfp::units::as_dimless(tolerances.nt_add);
    }

    CompleteConnectionRetentionDecision retain_exact_complete_connection(
          CompleteConnectionRetention& retention
        , SearchConnection             connection
        , const SearchParams&          params
        , double                       fare_scale
    ) {
        auto candidate = make_complete_connection_alternative(
              std::move(connection)
            , params
            , fare_scale
        );

        for (const auto& known : retention.alternatives) {
            if (same_connection_trace(known.connection, candidate.connection)) {
                return CompleteConnectionRetentionDecision{
                      .accepted          = false
                    , .removed_dominated = 0
                };
            }
            if (complete_connection_dominates(known.metrics, candidate.metrics)) {
                return CompleteConnectionRetentionDecision{
                      .accepted          = false
                    , .removed_dominated = 0
                };
            }
        }

        const auto before = retention.alternatives.size();
        retention.alternatives.erase(
              std::remove_if(
                    retention.alternatives.begin()
                  , retention.alternatives.end()
                  , [&](const CompleteConnectionAlternative& known) {
                        return complete_connection_dominates(candidate.metrics, known.metrics);
                    }
                )
            , retention.alternatives.end()
        );
        const auto removed = before - retention.alternatives.size();
        retention.alternatives.push_back(std::move(candidate));
        return CompleteConnectionRetentionDecision{
              .accepted          = true
            , .removed_dominated = removed
        };
    }

    std::vector<SearchConnection> finalize_complete_connection_retention(
          const CompleteConnectionRetention& retention
        , const ChoiceTolerances&            tolerances
        , ChoiceRolloutStage                 rollout_stage
    ) {
        std::vector<CompleteConnectionAlternative> retained;
        retained.reserve(retention.alternatives.size());

        if (rollout_stage == ChoiceRolloutStage::ExactOnly) {
            retained = retention.alternatives;
            return materialize_sorted_connections(std::move(retained));
        }

        const auto summary = summarize_complete_connection_metrics(retention.alternatives);
        for (const auto& alternative : retention.alternatives) {
            if (within_complete_connection_tolerances(
                  alternative.metrics
                , summary
                , tolerances
            )) {
                retained.push_back(alternative);
            }
        }
        return materialize_sorted_connections(std::move(retained));
    }

    std::vector<SearchConnection> refine_complete_connection_ptrs(
          const std::vector<const SearchConnection*>& connections
        , const SearchParams&                         params
        , double                                      fare_scale
        , ChoiceRolloutStage                          rollout_stage
    ) {
        CompleteConnectionRetention retention;
        retention.alternatives.reserve(connections.size());
        for (const auto* connection : connections) {
            (void)retain_exact_complete_connection(
                  retention
                , *connection
                , params
                , fare_scale
            );
        }
        return finalize_complete_connection_retention(
              retention
            , params.choice_tolerances
            , rollout_stage
        );
    }

}  // namespace timetable::domain::assignment
