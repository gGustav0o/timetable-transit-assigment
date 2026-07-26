#include "timetable/domain/assignment/complete_connection_retention.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <utility>

#include <mathfp/core/error.hpp>
#include <mathfp/core/try.hpp>
#include <mathfp/types/units.hpp>

namespace timetable::domain::assignment {
    namespace {

        [[nodiscard]] ConnectionImpedanceComponents complete_connection_base_components(
            const ConnectionMetrics& connection_metrics
        ) noexcept {
            return ConnectionImpedanceComponents{
                  .in_vehicle_time    = connection_metrics.in_vehicle_time
                , .access_time        = connection_metrics.access_time
                , .egress_time        = connection_metrics.egress_time
                , .transfer_walk_time = connection_metrics.transfer_walk_time
                , .transfer_wait_time = connection_metrics.transfer_wait_time
                , .transfer_count     = connection_metrics.transfer_count
                , .fare               = connection_metrics.fare
            };
        }

        [[nodiscard]] mathfp::Expected<CapacityExposure> complete_connection_capacity_exposure(
              const SearchConnection& connection
            , const SearchCostContext& search_cost
            , IntervalId               interval
        ) {
            switch (search_cost.mode) {
                case SearchCostMode::BaseOnly:
                    return CapacityExposure{ Time{ 0.0 } };

                case SearchCostMode::CapacityAware:
                    return search_capacity_exposure(
                          connection
                        , interval
                        , search_cost.capacity
                    );
            }

            return mathfp::unexpected(
                mathfp::invalid_arg("unknown search cost mode")
                    .ctx("mode", static_cast<std::int64_t>(search_cost.mode))
            );
        }

        [[nodiscard]] mathfp::Expected<CompleteConnectionAlternative> make_complete_connection_alternative(
              SearchConnection          connection
            , const SearchCostContext&  search_cost
            , IntervalId                interval
        );

        [[nodiscard]] mathfp::Expected<CompleteConnectionMetrics> complete_connection_metrics_unchecked_context(
              const SearchConnection&  connection
            , const SearchCostContext& search_cost
            , IntervalId               interval
        ) {
            const auto connection_metrics = metrics_of(connection);
            MATHFP_TRY_LET(
                  CapacityExposure
                , exposure
                , complete_connection_capacity_exposure(
                      connection
                    , search_cost
                    , interval
                )
            );
            const auto components = SearchCostComponents{
                  .base = complete_connection_base_components(connection_metrics)
                , .capacity_exposure = exposure
            };
            MATHFP_TRY_LET(
                  double
                , impedance
                , search_impedance(components, search_cost)
            );
            return CompleteConnectionMetrics{
                  .departure   = connection_metrics.departure_time
                , .arrival     = connection_metrics.arrival_time
                , .journey_time = connection_metrics.journey_time
                , .transfers    = connection_metrics.transfer_count
                , .impedance    = impedance
            };
        }

        [[nodiscard]] mathfp::Expected<CompleteConnectionAlternative> make_complete_connection_alternative(
              SearchConnection          connection
            , const SearchCostContext&  search_cost
            , IntervalId                interval
        ) {
            MATHFP_TRY_LET(
                  CompleteConnectionMetrics
                , metrics
                , complete_connection_metrics_unchecked_context(
                      connection
                    , search_cost
                    , interval
                )
            );
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

    mathfp::Expected<CompleteConnectionMetrics> complete_connection_metrics(
          const SearchConnection&  connection
        , const SearchCostContext& search_cost
        , IntervalId               interval
    ) {
        MATHFP_TRY(validate_search_cost_context(search_cost));
        return complete_connection_metrics_unchecked_context(
              connection
            , search_cost
            , interval
        );
    }

    bool is_direct_connection(
        const CompleteConnectionMetrics& metrics
    ) noexcept {
        return metrics.transfers.get() == 0;
    }

    bool complete_connection_can_dominate(
          const CompleteConnectionDominanceConfig& config
        , const CompleteConnectionMetrics&         metrics
    ) noexcept {
        return !config.deactivate_dominance_of_direct_connections
            || !is_direct_connection(metrics);
    }

    bool complete_connection_dominates(
          const CompleteConnectionDominanceConfig& config
        , const CompleteConnectionMetrics&         lhs
        , const CompleteConnectionMetrics&         rhs
    ) noexcept {
        if (!complete_connection_can_dominate(config, lhs)) {
            return false;
        }

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

    bool complete_connection_dominates(
          const CompleteConnectionMetrics& lhs
        , const CompleteConnectionMetrics& rhs
    ) noexcept {
        return complete_connection_dominates(
              CompleteConnectionDominanceConfig{}
            , lhs
            , rhs
        );
    }

    bool within_complete_connection_tolerances(
          const CompleteConnectionMetrics&       metrics
        , const CompleteConnectionMetricSummary& summary
        , const ChoiceTolerances&                tolerances
    ) noexcept {
        if (summary.empty) {
            return false;
        }
        //tex:
        // Final connection-choice filtering uses minima over complete OD
        // alternatives, not over node-local prefixes:
        // $$IMP(c)\le p_1\min IMP+p_2,\qquad JT(c)\le q_1\min JT+q_2,\qquad NT(c)\le r_1\min NT+r_2.$$
        // This is where user-defined stricter conditions remove illogical
        // complete connections after branch-and-bound search.
        return
               metrics.impedance
            <= tolerances.imp_mult.value()
             * summary.min_impedance
             + tolerances.imp_add.value()

            && metrics.journey_time.value()
            <= tolerances.jt_mult.value()
             * summary.min_journey_time
             + tolerances.jt_add.seconds()

            && static_cast<double>(metrics.transfers.get())
            <= tolerances.nt_mult.value()
             * summary.min_transfers
             + tolerances.nt_add.value();
    }

    mathfp::Expected<CompleteConnectionRetentionDecision> retain_exact_complete_connection(
          CompleteConnectionRetention& retention
        , SearchConnection             connection
        , const SearchCostContext&     search_cost
        , IntervalId                   interval
        , const CompleteConnectionDominanceConfig& dominance_config
    ) {
        MATHFP_TRY_LET(
              CompleteConnectionAlternative
            , candidate
            , make_complete_connection_alternative(
                  std::move(connection)
                , search_cost
                , interval
            )
        );

        for (const auto& known : retention.alternatives) {
            if (same_connection_trace(known.connection, candidate.connection)) {
                return CompleteConnectionRetentionDecision{
                      .accepted          = false
                    , .removed_dominated = 0
                };
            }
            if (complete_connection_dominates(
                  dominance_config
                , known.metrics
                , candidate.metrics
            )) {
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
                        return complete_connection_dominates(
                              dominance_config
                            , candidate.metrics
                            , known.metrics
                        );
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

    mathfp::Expected<CompleteConnectionRetentionDecision> retain_exact_complete_connection(
          CompleteConnectionRetention& retention
        , SearchConnection             connection
        , const SearchCostContext&     search_cost
        , IntervalId                   interval
    ) {
        return retain_exact_complete_connection(
              retention
            , std::move(connection)
            , search_cost
            , interval
            , CompleteConnectionDominanceConfig{}
        );
    }

    std::vector<SearchConnection> finalize_complete_connection_retention(
          const CompleteConnectionRetention& retention
        , const ChoiceTolerances&            tolerances
        , ChoiceRolloutStage                 rollout_stage
    ) {
        //tex:
        // Choice finalization re-evaluates the tree's retained complete
        // connections. `ExactOnly` keeps exact nondominated alternatives; the
        // full whole-connection choice stage additionally applies OD-local whole-connection
        // tolerance bounds before materializing the chosen connection set.
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

    mathfp::Expected<std::vector<SearchConnection>> refine_complete_connection_ptrs(
          const std::vector<const SearchConnection*>& connections
        , const SearchCostContext&                    search_cost
        , IntervalId                                  interval
        , const ChoiceTolerances&                     tolerances
        , ChoiceRolloutStage                          rollout_stage
    ) {
        CompleteConnectionRetention retention;
        retention.alternatives.reserve(connections.size());
        for (const auto* connection : connections) {
            MATHFP_TRY(retain_exact_complete_connection(
                  retention
                , *connection
                , search_cost
                , interval
            ));
        }
        return finalize_complete_connection_retention(
              retention
            , tolerances
            , rollout_stage
        );
    }

}  // namespace timetable::domain::assignment
