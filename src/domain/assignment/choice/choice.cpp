#include "timetable/domain/assignment/choice/choice.hpp"

#include <algorithm>
#include <iterator>
#include <limits>
#include <span>
#include <utility>
#include <vector>

#include <mathfp/types/units.hpp>

#include <fmt/format.h>

#include "../detail/grouping.hpp"
#include "timetable/infra/progress_bus.hpp"

namespace timetable::domain::assignment {
    namespace {

        struct ChoiceGroupStats final {
            double min_impedance   { std::numeric_limits<double>::infinity() };
            double min_journey_time{ std::numeric_limits<double>::infinity() };
            double min_transfers   { std::numeric_limits<double>::infinity() };
        };

        bool choice_dominates(
              const DiscoveredConnection& lhs
            , const DiscoveredConnection& rhs
        ) noexcept {
            const auto no_worse =
                   lhs.departure.value() >= rhs.departure.value()
                && lhs.arrival  .value() <= rhs.arrival  .value()
                && lhs.impedance         <= rhs.impedance
                && lhs.transfers.get()   <= rhs.transfers.get();

            const auto strictly_better =
                   lhs.departure.value() > rhs.departure.value()
                || lhs.arrival  .value() < rhs.arrival  .value()
                || lhs.impedance         < rhs.impedance
                || lhs.transfers.get()   < rhs.transfers.get();

            return no_worse && strictly_better;
        }

        bool is_choice_relevant(
              std::span<const DiscoveredConnection> connections
            , std::size_t                           candidate_index
        ) noexcept {
            const auto& candidate = connections[candidate_index];
            for (std::size_t i = 0; i < connections.size(); ++i) {
                if (i == candidate_index) {
                    continue;
                }
                if (choice_dominates(connections[i], candidate)) {
                    return false;
                }
            }
            return true;
        }

        ChoiceGroupStats collect_choice_group_stats(
            std::span<const DiscoveredConnection> connections
        ) noexcept {
            ChoiceGroupStats stats;
            for (const auto& connection : connections) {
                stats.min_impedance    = std::min(stats.min_impedance, connection.impedance);
                stats.min_journey_time
                    = std::min(stats.min_journey_time, connection.journey_time.value());
                stats.min_transfers = std::min(
                      stats.min_transfers
                    , static_cast<double>(connection.transfers.get())
                );
            }
            return stats;
        }

        bool within_choice_tolerances(
              const DiscoveredConnection& connection
            , const ChoiceGroupStats&     stats
            , const ChoiceTolerances&     tolerances
        ) noexcept {
            // TODO: ?
            return
                   connection.impedance
                <= mathfp::units::as_dimless(tolerances.imp_mult)
                 * stats.min_impedance
                 + mathfp::units::as_dimless(tolerances.imp_add)

                && connection.journey_time.value()
                <= mathfp::units::as_dimless(tolerances.jt_mult) * stats.min_journey_time
                 + mathfp::units::as_dimless(tolerances.jt_add)

                && static_cast<double>(connection.transfers.get())
                <= mathfp::units::as_dimless(tolerances.nt_mult) * stats.min_transfers
                 + mathfp::units::as_dimless(tolerances.nt_add);
        }

        std::vector<DiscoveredConnection> filter_choice_group(
              std::vector<DiscoveredConnection> connections
            , const ChoiceTolerances&           tolerances
            , const ChoiceConfig&               config
        ) {
            std::vector<DiscoveredConnection> relevant;
            relevant.reserve(connections.size());
            for (std::size_t i = 0; i < connections.size(); ++i) {
                if (is_choice_relevant(connections, i)) {
                    relevant.push_back(std::move(connections[i]));
                }
            }

            if (config.rollout_stage == ChoiceRolloutStage::ExactOnly) {
                std::sort(
                      relevant.begin()
                    , relevant.end()
                    , [](const DiscoveredConnection& lhs, const DiscoveredConnection& rhs) {
                        if (lhs.departure != rhs.departure) {
                            return lhs.departure.value() < rhs.departure.value();
                        }
                        if (lhs.arrival != rhs.arrival) {
                            return lhs.arrival.value() < rhs.arrival.value();
                        }
                        if (lhs.impedance != rhs.impedance) {
                            return lhs.impedance < rhs.impedance;
                        }
                        if (lhs.transfers != rhs.transfers) {
                            return lhs.transfers.get() < rhs.transfers.get();
                        }
                        return lhs.segments < rhs.segments;
                    }
                );
                return relevant;
            }

            const auto stats = collect_choice_group_stats(relevant);
            std::vector<DiscoveredConnection> chosen;
            chosen.reserve(relevant.size());
            for (auto& connection : relevant) {
                if (within_choice_tolerances(connection, stats, tolerances)) {
                    chosen.push_back(std::move(connection));
                }
            }

            std::sort(
                  chosen.begin()
                , chosen.end()
                , [](const DiscoveredConnection& lhs, const DiscoveredConnection& rhs) {
                    // TODO: ?
                    if (lhs.departure != rhs.departure) {
                        return lhs.departure.value() < rhs.departure.value();
                    }
                    if (lhs.arrival != rhs.arrival) {
                        return lhs.arrival.value() < rhs.arrival.value();
                    }
                    if (lhs.impedance != rhs.impedance) {
                        return lhs.impedance < rhs.impedance;
                    }
                    if (lhs.transfers != rhs.transfers) {
                        return lhs.transfers.get() < rhs.transfers.get();
                    }
                    return lhs.segments < rhs.segments;
                }
            );

            return chosen;
        }

    }  // namespace

    mathfp::Expected<ConnectionChoiceResult> choose_connections(
          const ConnectionSearchResult& search_result
        , const SearchParams&           params
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
                , static_cast<std::int64_t>(config.rollout_stage)
            )
            , LogLevel::Info
        );

        ConnectionChoiceResult result;
        const auto groups = detail::grouping::group_connections_by_od(search_result.connections);
        for (const auto& [key, group_connections] : groups) {
            auto chosen = filter_choice_group(group_connections, params.choice_tolerances, config);
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
