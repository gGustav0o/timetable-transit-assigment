#include "timetable/domain/assignment/choice/choice.hpp"

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <limits>
#include <span>
#include <vector>

#include <mathfp/types/units.hpp>

#include <fmt/format.h>

#include "../detail/grouping.hpp"
#include "timetable/domain/impedance.hpp"
#include "timetable/infra/progress_bus.hpp"

namespace timetable::domain::assignment {
    namespace {

        struct ChoiceMetrics final {
            Time          departure{};
            Time          arrival{};
            Time          journey_time{};
            TransferCount transfers{};
            double        impedance{};
        };

        struct ChoiceMetricSummary final {
            double min_impedance   { std::numeric_limits<double>::infinity() };
            double min_journey_time{ std::numeric_limits<double>::infinity() };
            double min_transfers   { std::numeric_limits<double>::infinity() };
        };

        struct ChoiceAlternative final {
            const SearchConnection* connection{};
            ChoiceMetrics           metrics{};
        };

        [[nodiscard]] double choice_impedance(
              const ConnectionMetrics& connection_metrics
            , const SearchParams&         params
            , double                      fare_scale
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

        [[nodiscard]] ChoiceMetrics derive_choice_metrics(
              const SearchConnection& connection
            , const SearchParams&     params
            , double                  fare_scale
        ) {
            const auto connection_metrics = metrics_of(connection);
            return ChoiceMetrics{
                  .departure   = connection_metrics.departure_time
                , .arrival     = connection_metrics.arrival_time
                , .journey_time = connection_metrics.journey_time
                , .transfers    = connection_metrics.transfer_count
                , .impedance    = choice_impedance(connection_metrics, params, fare_scale)
            };
        }

        [[nodiscard]] std::vector<ChoiceAlternative> derive_choice_alternatives(
              const std::vector<const SearchConnection*>& connections
            , const SearchParams&               params
            , double                            fare_scale
        ) {
            std::vector<ChoiceAlternative> alternatives;
            alternatives.reserve(connections.size());
            for (const auto* connection : connections) {
                const auto metrics = derive_choice_metrics(*connection, params, fare_scale);
                alternatives.push_back(
                    ChoiceAlternative{
                          .connection = connection
                        , .metrics    = metrics
                    }
                );
            }
            return alternatives;
        }

        bool choice_dominates(
              const ChoiceAlternative& lhs
            , const ChoiceAlternative& rhs
        ) noexcept {
            const auto no_worse =
                   lhs.metrics.departure.value() >= rhs.metrics.departure.value()
                && lhs.metrics.arrival  .value() <= rhs.metrics.arrival  .value()
                && lhs.metrics.impedance          <= rhs.metrics.impedance
                && lhs.metrics.transfers.get()    <= rhs.metrics.transfers.get();

            const auto strictly_better =
                   lhs.metrics.departure.value() > rhs.metrics.departure.value()
                || lhs.metrics.arrival  .value() < rhs.metrics.arrival  .value()
                || lhs.metrics.impedance          < rhs.metrics.impedance
                || lhs.metrics.transfers.get()    < rhs.metrics.transfers.get();

            return no_worse && strictly_better;
        }

        bool is_choice_relevant(
              std::span<const ChoiceAlternative> alternatives
            , std::size_t                           candidate_index
        ) noexcept {
            const auto& candidate = alternatives[candidate_index];
            for (std::size_t i = 0; i < alternatives.size(); ++i) {
                if (i == candidate_index) {
                    continue;
                }
                if (choice_dominates(alternatives[i], candidate)) {
                    return false;
                }
            }
            return true;
        }

        ChoiceMetricSummary summarize_choice_metrics(
            std::span<const ChoiceAlternative> alternatives
        ) noexcept {
            ChoiceMetricSummary stats;
            for (const auto& alternative : alternatives) {
                stats.min_impedance    = std::min(stats.min_impedance, alternative.metrics.impedance);
                stats.min_journey_time
                    = std::min(stats.min_journey_time, alternative.metrics.journey_time.value());
                stats.min_transfers = std::min(
                      stats.min_transfers
                    , static_cast<double>(alternative.metrics.transfers.get())
                );
            }
            return stats;
        }

        bool within_choice_tolerances(
              const ChoiceAlternative&         alternative
            , const ChoiceMetricSummary&       stats
            , const ChoiceTolerances&          tolerances
        ) noexcept {
            return
                   alternative.metrics.impedance
                <= mathfp::units::as_dimless(tolerances.imp_mult)
                 * stats.min_impedance
                 + mathfp::units::as_dimless(tolerances.imp_add)

                && alternative.metrics.journey_time.value()
                <= mathfp::units::as_dimless(tolerances.jt_mult) * stats.min_journey_time
                 + mathfp::units::as_dimless(tolerances.jt_add)

                && static_cast<double>(alternative.metrics.transfers.get())
                <= mathfp::units::as_dimless(tolerances.nt_mult) * stats.min_transfers
                 + mathfp::units::as_dimless(tolerances.nt_add);
        }

        bool connection_order_less(
              const ChoiceAlternative& lhs
            , const ChoiceAlternative& rhs
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
            return connection_segment_trace(*lhs.connection)
                < connection_segment_trace(*rhs.connection);
        }

        std::vector<SearchConnection> filter_choice_group(
              const std::vector<const SearchConnection*>& connections
            , const SearchParams&               params
            , double                            fare_scale
            , const ChoiceTolerances&           tolerances
            , const ChoiceConfig&               config
        ) {
            auto evaluated = derive_choice_alternatives(
                  connections
                , params
                , fare_scale
            );

            std::vector<std::size_t> relevant_indices;
            relevant_indices.reserve(evaluated.size());
            for (std::size_t i = 0; i < evaluated.size(); ++i) {
                if (is_choice_relevant(evaluated, i)) {
                    relevant_indices.push_back(i);
                }
            }

            std::vector<ChoiceAlternative> relevant;
            relevant.reserve(relevant_indices.size());
            for (const auto index : relevant_indices) {
                relevant.push_back(evaluated[index]);
            }

            if (config.rollout_stage == ChoiceRolloutStage::ExactOnly) {
                std::sort(relevant.begin(), relevant.end(), connection_order_less);
                std::vector<SearchConnection> chosen;
                chosen.reserve(relevant.size());
                for (const auto& alternative : relevant) {
                    chosen.push_back(*alternative.connection);
                }
                return chosen;
            }

            const auto stats = summarize_choice_metrics(relevant);
            std::vector<ChoiceAlternative> filtered;
            filtered.reserve(relevant.size());
            for (const auto& alternative : relevant) {
                if (within_choice_tolerances(alternative, stats, tolerances)) {
                    filtered.push_back(alternative);
                }
            }

            std::sort(filtered.begin(), filtered.end(), connection_order_less);

            std::vector<SearchConnection> chosen;
            chosen.reserve(filtered.size());
            for (const auto& alternative : filtered) {
                chosen.push_back(*alternative.connection);
            }

            return chosen;
        }

    }  // namespace

    mathfp::Expected<ConnectionChoiceResult> choose_connections(
          const ConnectionSearchResult& search_result
        , const SearchParams&           params
        , double                        fare_scale
        , const ChoiceConfig&           config
    ) {
        using timetable::infra::LogLevel;
        using timetable::infra::progress::both;
        using timetable::infra::progress::log;

        both("choice: pruning connections");
        log(
            fmt::format(
                  "choice input: connections = {:>8}  rollout_stage = {}"
                , search_result.connections.size()
                , to_string(config.rollout_stage)
            )
            , LogLevel::Info
        );

        ConnectionChoiceResult result;
        const auto groups = detail::grouping::group_connection_ptrs_by_od(search_result.connections);
        for (const auto& [key, group_connections] : groups) {
            auto chosen = filter_choice_group(
                  group_connections
                , params
                , fare_scale
                , params.choice_tolerances
                , config
            );
            log(
                fmt::format(
                    "choice group: origin = {:>6}  destination = {:>6}"
                    "  input = {:>5}  chosen = {:>5}"
                    , key.origin.get()
                    , key.destination.get()
                    , group_connections.size()
                    , chosen.size()
                )
                , LogLevel::Info
            );
            result.connections.insert(
                  result.connections.end()
                , std::make_move_iterator(chosen.begin())
                , std::make_move_iterator(chosen.end())
            );
        }

        log(
            fmt::format(
                  "choice result: groups = {:>6}  connections = {:>8}"
                , groups.size()
                , result.connections.size()
            )
            , LogLevel::Info
        );
        both("choice: pruning connections done");
        return result;
    }

}  // namespace timetable::domain::assignment
