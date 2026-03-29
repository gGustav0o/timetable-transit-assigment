#include "timetable/domain/assignment/validation.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <map>
#include <span>
#include <utility>

#include <fmt/format.h>

#include <mathfp/core/error.hpp>
#include <mathfp/core/numeric_tolerance.hpp>
#include <mathfp/core/try.hpp>
#include <mathfp/core/unit.hpp>

#include "../detail/validation_common.hpp"

namespace timetable::domain::assignment {
    namespace {

        [[nodiscard]] bool intervals_overlap(
              const TimeInterval& lhs
            , const TimeInterval& rhs
        ) noexcept {
            return lhs.start.value() < rhs.end.value()
                && rhs.start.value() < lhs.end.value();
        }

        [[nodiscard]] std::map<IntervalId, const TimeInterval*> build_interval_map(
            const InputModel& input
        ) {
            std::map<IntervalId, const TimeInterval*> intervals;
            for (const auto& interval : input.intervals) {
                intervals.emplace(interval.id, &interval);
            }
            return intervals;
        }

        [[nodiscard]] std::map<ZoneId, const Zone*> build_zone_map(
            const InputModel& input
        ) {
            std::map<ZoneId, const Zone*> zones;
            for (const auto& zone : input.zones) {
                zones.emplace(zone.id, &zone);
            }
            return zones;
        }

        mathfp::Expected<std::map<detail::validation::DemandKey, const DemandEntry*>> validate_and_index_demand_entries(
              const InputModel&                     input
            , std::span<const DiscoveredConnection> choice_connections
            , bool                                  emit_warnings
        ) {
            const auto intervals     = build_interval_map(input);
            const auto zones         = build_zone_map(input);
            const auto choice_counts = detail::validation::count_connections_by_od(choice_connections);

            std::map<detail::validation::DemandKey, const DemandEntry*> demand_by_key;
            for (const auto& demand : input.demand) {
                if (!std::isfinite(demand.passengers) || demand.passengers < 0.0) {
                    return mathfp::unexpected(
                        mathfp::invalid_arg("demand passengers must be finite and non-negative")
                            .ctx("origin"     , demand.origin.get())
                            .ctx("destination", demand.destination.get())
                            .ctx("interval_id", demand.interval.get())
                            .ctx("passengers" , demand.passengers)
                    );
                }
                if (!intervals.contains(demand.interval)) {
                    return mathfp::unexpected(
                        mathfp::invalid_arg("demand references unknown time interval")
                            .ctx("origin"     , demand.origin.get())
                            .ctx("destination", demand.destination.get())
                            .ctx("interval_id", demand.interval.get())
                    );
                }
                if (!zones.contains(demand.origin) || !zones.contains(demand.destination)) {
                    return mathfp::unexpected(
                        mathfp::invalid_arg("demand references unknown zone")
                            .ctx("origin"     , demand.origin.get())
                            .ctx("destination", demand.destination.get())
                            .ctx("interval_id", demand.interval.get())
                    );
                }

                const auto key = detail::validation::demand_key(demand);
                if (!demand_by_key.emplace(key, &demand).second) {
                    return mathfp::unexpected(
                        mathfp::invalid_arg("duplicate demand entry for the same origin/destination/interval")
                            .ctx("origin"     , demand.origin.get())
                            .ctx("destination", demand.destination.get())
                            .ctx("interval_id", demand.interval.get())
                    );
                }

                if (emit_warnings && demand.origin == demand.destination) {
                    detail::validation::warn(fmt::format(
                          "split input: demand entry has identical origin and destination zone {} for interval {}"
                        , demand.origin.get()
                        , demand.interval.get()
                    ));
                }
                if (emit_warnings && mathfp::almost_zero(demand.passengers)) {
                    detail::validation::warn(fmt::format(
                          "split input: demand entry origin={} destination={} interval={} has zero passengers"
                        , demand.origin.get()
                        , demand.destination.get()
                        , demand.interval.get()
                    ));
                }
                if (emit_warnings
                    && !choice_counts.contains(detail::validation::OdKey{ demand.origin, demand.destination })
                    && demand.passengers > 0.0) {
                    detail::validation::warn(fmt::format(
                          "split input: no chosen connections for demand origin={} destination={} interval={}"
                        , demand.origin.get()
                        , demand.destination.get()
                        , demand.interval.get()
                    ));
                }
            }

            return demand_by_key;
        }

    }  // namespace

    mathfp::Expected<mathfp::Unit> validate_split_step_input(
          const ConnectionChoiceResult& choice_result
        , const InputModel&             input
    ) {
        if (input.intervals.empty()) {
            return mathfp::unexpected(
                mathfp::invalid_arg("split step requires non-empty time intervals")
            );
        }
        if (input.demand.empty()) {
            return mathfp::unexpected(
                mathfp::invalid_arg("split step requires non-empty demand entries")
            );
        }

        std::map<IntervalId, const TimeInterval*> intervals;
        for (const auto& interval : input.intervals) {
            if (!intervals.emplace(interval.id, &interval).second) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("duplicate time interval id")
                        .ctx("interval_id", interval.id.get())
                );
            }
            if (!(interval.start.value() < interval.end.value())) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("time interval must satisfy start < end")
                        .ctx("interval_id", interval.id.get())
                        .ctx("start"      , interval.start.value())
                        .ctx("end"        , interval.end.value())
                );
            }
        }

        for (std::size_t i = 0; i < input.intervals.size(); ++i) {
            for (std::size_t j = i + 1; j < input.intervals.size(); ++j) {
                const auto& lhs = input.intervals[i];
                const auto& rhs = input.intervals[j];
                if (intervals_overlap(lhs, rhs)) {
                    detail::validation::warn(fmt::format(
                          "split input: intervals {} and {} overlap in time"
                        , lhs.id.get()
                        , rhs.id.get()
                    ));
                }
            }
        }

        if (choice_result.connections.empty()) {
            detail::validation::warn("split input: no chosen connections are available; split output will be empty");
        }

        MATHFP_TRY(validate_and_index_demand_entries(input, choice_result.connections, true));
        return mathfp::kUnit;
    }

    mathfp::Expected<mathfp::Unit> validate_split_step_output(
          const DemandSplitResult&      split_result
        , const ConnectionChoiceResult& choice_result
        , const InputModel&             input
    ) {
        const auto choice_trace_map = detail::validation::trace_index_map(choice_result.connections);
        const auto demand_by_key_result = validate_and_index_demand_entries(
              input
            , choice_result.connections
            , false
        );
        if (!demand_by_key_result) {
            return mathfp::unexpected(std::move(demand_by_key_result.error()));
        }
        const auto& demand_by_key = *demand_by_key_result;
        const auto choice_counts  = detail::validation::count_connections_by_od(choice_result.connections);

        std::map<detail::validation::DemandKey, double> probability_sum_by_key;
        std::map<detail::validation::DemandKey, double> passengers_sum_by_key;

        for (std::size_t i = 0; i < split_result.shares.size(); ++i) {
            const auto& share = split_result.shares[i];
            const auto key = detail::validation::DemandKey{
                  .origin      = share.origin
                , .destination = share.destination
                , .interval    = share.interval
            };

            if (!demand_by_key.contains(key)) {
                return mathfp::unexpected(
                    mathfp::internal_error("split output contains a share without matching demand entry")
                        .ctx("share_index", static_cast<std::int64_t>(i))
                        .ctx("origin"     , share.origin.get())
                        .ctx("destination", share.destination.get())
                        .ctx("interval_id", share.interval.get())
                );
            }
            if (!choice_trace_map.contains(detail::validation::connection_trace_key(share.connection))) {
                return mathfp::unexpected(
                    mathfp::internal_error("split output contains a connection that was not present in choice output")
                        .ctx("share_index", static_cast<std::int64_t>(i))
                        .ctx("origin"     , share.origin.get())
                        .ctx("destination", share.destination.get())
                        .ctx("interval_id", share.interval.get())
                );
            }
            if (!std::isfinite(share.passengers) || share.passengers < 0.0
                || !detail::validation::valid_probability(share.probability)
                || !std::isfinite(share.independence) || share.independence <= 0.0 || share.independence > 1.0
                || !detail::validation::is_finite_non_negative(share.split_impedance)) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("split output contains non-finite or out-of-range share metrics")
                        .ctx("share_index"    , static_cast<std::int64_t>(i))
                        .ctx("passengers"     , share.passengers)
                        .ctx("probability"    , share.probability)
                        .ctx("independence"   , share.independence)
                        .ctx("split_impedance", share.split_impedance)
                );
            }

            probability_sum_by_key[key] += share.probability;
            passengers_sum_by_key[key] += share.passengers;
        }

        for (const auto& [key, demand] : demand_by_key) {
            const auto has_available_choice = choice_counts.contains(detail::validation::OdKey{
                  .origin      = key.origin
                , .destination = key.destination
            });
            if (!has_available_choice || demand->passengers <= 0.0) {
                continue;
            }

            if (!probability_sum_by_key.contains(key)) {
                return mathfp::unexpected(
                    mathfp::internal_error("split output is missing shares for a demand entry with available chosen connections")
                        .ctx("origin"     , key.origin.get())
                        .ctx("destination", key.destination.get())
                        .ctx("interval_id", key.interval.get())
                );
            }

            const auto probability_sum = probability_sum_by_key[key];
            const auto passengers_sum  = passengers_sum_by_key[key];
            if (!detail::validation::almost_equal_scalar(probability_sum, 1.0)) {
                return mathfp::unexpected(
                    mathfp::internal_error("split probabilities do not sum to one")
                        .ctx("origin"         , key.origin.get())
                        .ctx("destination"    , key.destination.get())
                        .ctx("interval_id"    , key.interval.get())
                        .ctx("probability_sum", probability_sum)
                );
            }
            if (!detail::validation::almost_equal_scalar(passengers_sum, demand->passengers)) {
                return mathfp::unexpected(
                    mathfp::internal_error("split passengers do not conserve demand")
                        .ctx("origin"           , key.origin.get())
                        .ctx("destination"      , key.destination.get())
                        .ctx("interval_id"      , key.interval.get())
                        .ctx("passengers_sum"   , passengers_sum)
                        .ctx("demand_passengers", demand->passengers)
                );
            }
        }

        if (split_result.shares.empty()) {
            detail::validation::warn("split output: no demand shares were produced");
        }

        return mathfp::kUnit;
    }

}  // namespace timetable::domain::assignment
