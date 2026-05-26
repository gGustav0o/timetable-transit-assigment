#include "timetable/domain/assignment/validation.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <fmt/format.h>

#include <mathfp/core/error.hpp>
#include <mathfp/core/numeric_tolerance.hpp>
#include <mathfp/core/summation.hpp>
#include <mathfp/core/try.hpp>
#include <mathfp/core/unit.hpp>
#include <mathfp/types/units.hpp>

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
              const InputModel&                                 input
            , const std::map<detail::validation::DemandKey, std::size_t>& choice_counts
            , bool                                              emit_warnings
        ) {
            const auto intervals     = build_interval_map(input);
            const auto zones         = build_zone_map(input);

            std::map<detail::validation::DemandKey, const DemandEntry*> demand_by_key;
            for (const auto& demand : input.demand) {
                if (!std::isfinite(demand.passengers) || demand.passengers < 0.0) {
                    return mathfp::unexpected(
                        mathfp::invalid_arg("demand passengers must be finite and non-negative")
                            .ctx("origin"     , demand.origin     .get())
                            .ctx("destination", demand.destination.get())
                            .ctx("interval_id", demand.interval   .get())
                            .ctx("passengers" , demand.passengers)
                    );
                }
                if (!intervals.contains(demand.interval)) {
                    return mathfp::unexpected(
                        mathfp::invalid_arg("demand references unknown time interval")
                            .ctx("origin"     , demand.origin     .get())
                            .ctx("destination", demand.destination.get())
                            .ctx("interval_id", demand.interval   .get())
                    );
                }
                if (!zones.contains(demand.origin) || !zones.contains(demand.destination)) {
                    return mathfp::unexpected(
                        mathfp::invalid_arg("demand references unknown zone")
                            .ctx("origin"     , demand.origin     .get())
                            .ctx("destination", demand.destination.get())
                            .ctx("interval_id", demand.interval   .get())
                    );
                }

                const auto key = detail::validation::demand_key(demand);
                MATHFP_TRY(detail::validation::emplace_unique(
                      demand_by_key
                    , key
                    , &demand
                    , [&]() {
                        return mathfp::invalid_arg(
                            "duplicate demand entry for the same origin/destination/interval"
                        )
                            .ctx("origin"     , demand.origin     .get())
                            .ctx("destination", demand.destination.get())
                            .ctx("interval_id", demand.interval   .get());
                    }
                ));

                if (emit_warnings && demand.origin == demand.destination) {
                    detail::validation::warn(fmt::format(
                          "split input: demand entry has identical origin and destination zone {} for interval {}"
                        , demand.origin  .get()
                        , demand.interval.get()
                    ));
                }
                if (emit_warnings && mathfp::almost_zero(demand.passengers)) {
                    detail::validation::warn(fmt::format(
                          "split input: demand entry origin={} destination={} interval={} has zero passengers"
                        , demand.origin     .get()
                        , demand.destination.get()
                        , demand.interval   .get()
                    ));
                }
                if (emit_warnings
                    && !choice_counts.contains(key)
                    && demand.passengers > 0.0) {
                    detail::validation::warn(fmt::format(
                          "split input: no chosen connections for demand origin={} destination={} interval={}"
                        , demand.origin     .get()
                        , demand.destination.get()
                        , demand.interval   .get()
                    ));
                }
            }

            return demand_by_key;
        }

        [[nodiscard]] std::map<detail::validation::DemandKey, std::size_t> count_choice_task_connections_by_demand_key(
            const ConnectionChoiceResult& choice_result
        ) {
            std::map<detail::validation::DemandKey, std::size_t> counts;
            for (const auto& task_result : choice_result.task_results) {
                if (task_result.connections.empty()) {
                    continue;
                }
                counts[detail::validation::DemandKey{
                      .origin      = task_result.task.origin
                    , .destination = task_result.task.destination
                    , .interval    = task_result.task.interval.id
                }] += task_result.connections.size();
            }
            return counts;
        }

        using ChoiceTaskLookup = std::map<detail::validation::DemandKey, const ChoiceTaskResult*>;

        mathfp::Expected<ChoiceTaskLookup> build_choice_task_lookup(
            const ConnectionChoiceResult& choice_result
        ) {
            ChoiceTaskLookup lookup;
            for (const auto& task_result : choice_result.task_results) {
                const auto key = detail::validation::DemandKey{
                      .origin      = task_result.task.origin
                    , .destination = task_result.task.destination
                    , .interval    = task_result.task.interval.id
                };
                MATHFP_TRY(detail::validation::emplace_unique(
                      lookup
                    , key
                    , &task_result
                    , [&]() {
                        return mathfp::internal_error("choice output contains duplicate task for demand key")
                            .ctx("origin"     , key.origin     .get())
                            .ctx("destination", key.destination.get())
                            .ctx("interval_id", key.interval   .get());
                    }
                ));
            }
            return lookup;
        }

        mathfp::Expected<mathfp::Unit> validate_share_day_path_identity(
              const ConnectionDemandShare& share
            , std::size_t                  share_index
        ) {
            switch (share.source) {
                case DemandShareAlternativeSource::TimedConnection:
                    return mathfp::kUnit;

                case DemandShareAlternativeSource::DayPath: {
                    const auto support_signature =
                        day_path_signature_of(share.connection);
                    if (support_signature == share.day_path) {
                        return mathfp::kUnit;
                    }
                    return mathfp::unexpected(
                        mathfp::internal_error("split output day-path share signature disagrees with selected support connection")
                            .ctx("share_index", static_cast<std::int64_t>(share_index))
                            .ctx("origin"     , share.origin.get())
                            .ctx("destination", share.destination.get())
                            .ctx("interval_id", share.interval.get())
                    );
                }
            }

            return mathfp::unexpected(
                mathfp::internal_error("split output contains unsupported share alternative source")
                    .ctx("share_index", static_cast<std::int64_t>(share_index))
                    .ctx("source"     , static_cast<std::int64_t>(share.source))
            );
        }

        mathfp::Expected<mathfp::Unit> validate_split_choice_model_config_for_runtime(
            const SplitChoiceModelConfig& config
        ) {
            switch (config.model) {
                case SplitChoiceModel::Kirchhoff:
                case SplitChoiceModel::Logit:
                case SplitChoiceModel::BoxCox:
                    break;

                case SplitChoiceModel::Lohse:
                    return mathfp::unexpected(
                        mathfp::invalid_arg("Lohse split choice model is not implemented")
                            .ctx("choice_model", std::string(to_string(config.model)))
                    );

                default:
                    return mathfp::unexpected(
                        mathfp::invalid_arg("unsupported split choice model")
                            .ctx("choice_model", static_cast<std::int64_t>(config.model))
                    );
            }

            const auto exponent = mathfp::units::as_dimless(config.exponent);
            if (!std::isfinite(exponent) || exponent <= 0.0) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("split choice model exponent must be finite and positive")
                        .ctx("choice_model", std::string(to_string(config.model)))
                        .ctx("exponent", exponent)
                );
            }

            return mathfp::kUnit;
        }

        mathfp::Expected<mathfp::Unit> validate_split_impedance_transform_config_for_runtime(
            const SplitImpedanceTransformConfig& config
        ) {
            const auto boxcox_t = mathfp::units::as_dimless(config.boxcox_t);
            if (!std::isfinite(boxcox_t)) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("split impedance transform Box-Cox parameter must be finite")
                        .ctx(
                              "boxcox_transform_enabled"
                            , config.boxcox_transform_enabled ? "true" : "false"
                          )
                        .ctx("boxcox_t", boxcox_t)
                );
            }

            return mathfp::kUnit;
        }

        mathfp::Expected<mathfp::Unit> validate_split_independence_config_for_runtime(
            const SplitIndependenceConfig& config
        ) {
            const auto gamma = mathfp::units::as_dimless(config.gamma);
            if (!std::isfinite(gamma) || gamma < 0.0) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("split independence gamma must be finite and non-negative")
                        .ctx("enabled", config.enabled ? "true" : "false")
                        .ctx("gamma", gamma)
                );
            }

            const auto temporal_similarity_scale =
                mathfp::units::as_dimless(config.temporal_similarity_scale);
            if (!std::isfinite(temporal_similarity_scale)
                || temporal_similarity_scale <= 0.0) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("split independence temporal scale must be finite and positive")
                        .ctx("enabled", config.enabled ? "true" : "false")
                        .ctx("temporal_similarity_scale", temporal_similarity_scale)
                );
            }

            const auto higher_quality_scale =
                mathfp::units::as_dimless(config.higher_quality_scale);
            if (!std::isfinite(higher_quality_scale) || higher_quality_scale <= 0.0) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("split independence higher-quality scale must be finite and positive")
                        .ctx("enabled", config.enabled ? "true" : "false")
                        .ctx("higher_quality_scale", higher_quality_scale)
                );
            }

            const auto lower_quality_scale =
                mathfp::units::as_dimless(config.lower_quality_scale);
            if (!std::isfinite(lower_quality_scale) || lower_quality_scale <= 0.0) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("split independence lower-quality scale must be finite and positive")
                        .ctx("enabled", config.enabled ? "true" : "false")
                        .ctx("lower_quality_scale", lower_quality_scale)
                );
            }

            return mathfp::kUnit;
        }

        using VehicleJourneyItemCapacityLookup =
            std::map<VehicleJourneyItemKey, const VehicleJourneyItemCapacity*>;

        [[nodiscard]] VehicleJourneyItemCapacityLookup build_capacity_lookup(
            const VehicleJourneyItemCapacitySet& capacities
        ) {
            VehicleJourneyItemCapacityLookup lookup;
            for (const auto& capacity : capacities.items) {
                lookup.emplace(capacity.key, &capacity);
            }
            return lookup;
        }

        mathfp::Expected<mathfp::Unit> validate_capacity_for_occupied_item(
              const VehicleJourneyItemCapacityLookup& lookup
            , VehicleJourneyItemKey                   item
            , std::size_t                             connection_index
        ) {
            const auto it = lookup.find(item);
            if (it == lookup.end()) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("capacity-aware split alternative uses vehicle journey item without capacity")
                        .ctx("connection_index", static_cast<std::int64_t>(connection_index))
                        .ctx("trip_id", item.trip.get())
                        .ctx("from_index", item.from_index.get())
                );
            }

            if (!(it->second->total_capacity > 0.0)) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("capacity-aware split requires positive total capacity for every occupied item")
                        .ctx("connection_index", static_cast<std::int64_t>(connection_index))
                        .ctx("trip_id", item.trip.get())
                        .ctx("from_index", item.from_index.get())
                        .ctx("total_capacity", it->second->total_capacity)
                );
            }

            return mathfp::kUnit;
        }

        mathfp::Expected<mathfp::Unit> validate_capacity_coverage_for_choice_result(
              const ConnectionChoiceResult&       choice_result
            , const VehicleJourneyItemCapacitySet& capacities
        ) {
            MATHFP_TRY(validate_vehicle_journey_item_capacity_set(capacities));
            const auto capacity_lookup = build_capacity_lookup(capacities);

            std::size_t connection_index = 0;
            for (const auto& task_result : choice_result.task_results) {
                for (const auto& connection : task_result.connections) {
                    for (const auto& leg : canonical_connection(connection).trace.legs) {
                        if (!is_ride_leg(leg.kind)) {
                            continue;
                        }

                        MATHFP_TRY_LET(
                              std::vector<VehicleJourneyItemKey>
                            , occupied_items
                            , vehicle_journey_items_occupied(leg)
                        );

                        for (const auto& item : occupied_items) {
                            MATHFP_TRY(validate_capacity_for_occupied_item(
                                  capacity_lookup
                                , item
                                , connection_index
                            ));
                        }
                    }
                    ++connection_index;
                }
            }

            return mathfp::kUnit;
        }

    }  // namespace

    mathfp::Expected<mathfp::Unit> validate_split_step_input(
          const ConnectionChoiceResult& choice_result
        , const InputModel&             input
        , const SplitParams&            params
        , const DemandSegmentTimeConfig& demand_segment_time
    ) {
        MATHFP_TRY(validate_split_choice_model_config_for_runtime(params.choice_model));
        MATHFP_TRY(validate_split_impedance_transform_config_for_runtime(
            params.impedance_transform
        ));
        MATHFP_TRY(validate_split_independence_config_for_runtime(params.independence));
        MATHFP_TRY(validate_demand_segment_time_config(demand_segment_time));

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
            MATHFP_TRY(detail::validation::emplace_unique(
                  intervals
                , interval.id
                , &interval
                , [&]() {
                    return mathfp::invalid_arg("duplicate time interval id")
                        .ctx("interval_id", interval.id.get());
                }
            ));
            if (!(interval.start.value() < interval.end.value())) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("time interval must satisfy start < end")
                        .ctx("interval_id", interval.id.get())
                        .ctx("start"      , interval.start.value())
                        .ctx("end"        , interval.end  .value())
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

        MATHFP_TRY_LET(
              ChoiceTaskLookup
            , choice_task_lookup
            , build_choice_task_lookup(choice_result)
        );
        auto demand_by_key_result = validate_and_index_demand_entries(
              input
            , count_choice_task_connections_by_demand_key(choice_result)
            , true
        );
        if (!demand_by_key_result) {
            return mathfp::unexpected(std::move(demand_by_key_result.error()));
        }
        auto demand_by_key = std::move(*demand_by_key_result);

        for (const auto& [key, demand] : demand_by_key) {
            if (demand->passengers <= 0.0) {
                continue;
            }
            const auto task_it = choice_task_lookup.find(key);
            if (task_it == choice_task_lookup.end()) {
                return mathfp::unexpected(
                    mathfp::internal_error("split input is missing choice task for positive demand")
                        .ctx("origin"     , key.origin     .get())
                        .ctx("destination", key.destination.get())
                        .ctx("interval_id", key.interval   .get())
                );
            }
            if (task_it->second->connections.empty()) {
                detail::validation::warn(fmt::format(
                      "split input: choice task for positive demand origin={} destination={} interval={} has no alternatives"
                    , key.origin     .get()
                    , key.destination.get()
                    , key.interval   .get()
                ));
            }
        }
        return mathfp::kUnit;
    }

    mathfp::Expected<mathfp::Unit> validate_split_step_output(
          const DemandSplitResult&      split_result
        , const ConnectionChoiceResult& choice_result
        , const InputModel&             input
    ) {
        const auto choice_trace_map = detail::validation::trace_index_map(choice_result.connections);
        const auto choice_counts = count_choice_task_connections_by_demand_key(choice_result);
        MATHFP_TRY_LET(
              ChoiceTaskLookup
            , choice_task_lookup
            , build_choice_task_lookup(choice_result)
        );
        const auto demand_by_key_result = validate_and_index_demand_entries(
              input
            , choice_counts
            , false
        );
        if (!demand_by_key_result) {
            return mathfp::unexpected(std::move(demand_by_key_result.error()));
        }
        const auto& demand_by_key = *demand_by_key_result;

        std::map<detail::validation::DemandKey, mathfp::CompensatedSum<double>> probability_sum_by_key;
        std::map<detail::validation::DemandKey, mathfp::CompensatedSum<double>> passengers_sum_by_key;

        MATHFP_TRY(detail::validation::validate_each_index(
              split_result.shares
            , [&](const ConnectionDemandShare& share, std::size_t i)
                -> mathfp::Expected<mathfp::Unit> {
                const auto key = detail::validation::DemandKey{
                      .origin      = share.origin
                    , .destination = share.destination
                    , .interval    = share.interval
                };

                MATHFP_TRY(detail::validation::ensure_contains(
                      demand_by_key
                    , key
                    , [&]() {
                        return mathfp::internal_error(
                            "split output contains a share without matching demand entry"
                        )
                            .ctx("share_index", static_cast<std::int64_t>(i))
                            .ctx("origin"     , share.origin     .get())
                            .ctx("destination", share.destination.get())
                            .ctx("interval_id", share.interval   .get());
                    }
                ));
                MATHFP_TRY(detail::validation::ensure_contains(
                      choice_trace_map
                    , detail::validation::connection_trace_key(share.connection)
                    , [&]() {
                        return mathfp::internal_error(
                            "split output contains a connection that was not present in choice output"
                        )
                            .ctx("share_index", static_cast<std::int64_t>(i))
                            .ctx("origin"     , share.origin     .get())
                            .ctx("destination", share.destination.get())
                            .ctx("interval_id", share.interval   .get());
                    }
                ));
                const auto task_it = choice_task_lookup.find(key);
                if (task_it == choice_task_lookup.end()) {
                    return mathfp::unexpected(
                        mathfp::internal_error("split output share has no matching choice task")
                            .ctx("share_index", static_cast<std::int64_t>(i))
                            .ctx("origin"     , share.origin     .get())
                            .ctx("destination", share.destination.get())
                            .ctx("interval_id", share.interval   .get())
                    );
                }
                std::vector<const SearchConnection*> task_connections;
                task_connections.reserve(task_it->second->connections.size());
                for (const auto& connection : task_it->second->connections) {
                    task_connections.push_back(&connection);
                }
                const auto task_trace_map = detail::validation::trace_index_map(task_connections);
                MATHFP_TRY(detail::validation::ensure_contains(
                      task_trace_map
                    , detail::validation::connection_trace_key(share.connection)
                    , [&]() {
                        return mathfp::internal_error(
                            "split output share references a connection outside its choice task"
                        )
                            .ctx("share_index", static_cast<std::int64_t>(i))
                            .ctx("origin"     , share.origin     .get())
                            .ctx("destination", share.destination.get())
                            .ctx("interval_id", share.interval   .get());
                    }
                ));
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
                MATHFP_TRY(validate_share_day_path_identity(share, i));

                probability_sum_by_key[key].add(share.probability);
                passengers_sum_by_key[key].add(share.passengers);
                return mathfp::kUnit;
            }
        ));

        for (const auto& [key, demand] : demand_by_key) {
            const auto has_available_choice = choice_counts.contains(key);
            if (!has_available_choice || demand->passengers <= 0.0) {
                continue;
            }

            if (!probability_sum_by_key.contains(key)) {
                return mathfp::unexpected(
                    mathfp::internal_error("split output is missing shares for a demand entry with available chosen connections")
                        .ctx("origin"     , key.origin     .get())
                        .ctx("destination", key.destination.get())
                        .ctx("interval_id", key.interval   .get())
                );
            }

            const auto probability_sum = probability_sum_by_key[key].value();
            const auto passengers_sum  = passengers_sum_by_key[key].value();
            const auto share_count = choice_counts.at(key);
            if (!detail::validation::almost_equal_accumulated(probability_sum, 1.0, share_count)) {
                return mathfp::unexpected(
                    mathfp::internal_error("split probabilities do not sum to one")
                        .ctx("origin"         , key.origin     .get())
                        .ctx("destination"    , key.destination.get())
                        .ctx("interval_id"    , key.interval   .get())
                        .ctx("probability_sum", probability_sum)
                        .ctx("share_count"    , static_cast<std::int64_t>(share_count))
                );
            }
            if (!detail::validation::almost_equal_accumulated(passengers_sum, demand->passengers, share_count)) {
                return mathfp::unexpected(
                    mathfp::internal_error("split passengers do not conserve demand")
                        .ctx("origin"           , key.origin     .get())
                        .ctx("destination"      , key.destination.get())
                        .ctx("interval_id"      , key.interval   .get())
                        .ctx("passengers_sum"   , passengers_sum)
                        .ctx("demand_passengers", demand->passengers)
                        .ctx("share_count"      , static_cast<std::int64_t>(share_count))
                );
            }
        }

        if (split_result.shares.empty()) {
            detail::validation::warn("split output: no demand shares were produced");
        }

        return mathfp::kUnit;
    }

    mathfp::Expected<mathfp::Unit> validate_capacity_aware_split_step_input(
          const ConnectionChoiceResult& choice_result
        , const InputModel&             input
        , const SearchParams&           params
        , const DemandSegmentTimeConfig& demand_segment_time
        , const CapacityAwareAssignmentConfig& capacity_config
        , const VehicleJourneyItemCapacityInput& capacity_input
    ) {
        MATHFP_TRY(validate_split_step_input(
              choice_result
            , input
            , params.split
            , demand_segment_time
        ));
        MATHFP_TRY(validate_capacity_aware_assignment_config(capacity_config));
        MATHFP_TRY(validate_vehicle_journey_item_capacity_input(capacity_input));

        const auto split_factor = mathfp::units::as_dimless(
            params.split.perceived_journey_time.volume_capacity_ratio
        );

        if (!(split_factor > 0.0)) {
            return mathfp::unexpected(
                mathfp::invalid_arg("capacity-aware split step requires positive PerceivedJourneyTime.volCapRatioFactor")
                    .ctx("vol_cap_ratio_factor", split_factor)
            );
        }

        if (!capacity_config.capacity_aware_split_enabled) {
            return mathfp::unexpected(
                mathfp::invalid_arg("capacity-aware split step requires enabled capacity-aware split config")
                    .ctx("vol_cap_ratio_factor", split_factor)
            );
        }

        if (capacity_input.status != VehicleJourneyItemCapacityInputStatus::Loaded) {
            return mathfp::unexpected(
                mathfp::invalid_arg("capacity-aware split step requires loaded vehicle journey item capacity input")
                    .ctx("capacity_input_status", std::string(to_string(capacity_input.status)))
            );
        }

        return validate_capacity_coverage_for_choice_result(
              choice_result
            , capacity_input.capacities
        );
    }

    mathfp::Expected<mathfp::Unit> validate_capacity_aware_split_step_output(
          const CapacityAwareDemandSplitResult& result
        , const ConnectionChoiceResult&         choice_result
        , const InputModel&                     input
        , const CapacityAwareAssignmentConfig&  capacity_config
    ) {
        MATHFP_TRY(validate_capacity_aware_assignment_config(capacity_config));
        MATHFP_TRY(validate_split_step_output(
              result.split_result
            , choice_result
            , input
        ));
        MATHFP_TRY(validate_vehicle_journey_item_loads(result.split_loads));
        MATHFP_TRY(validate_vehicle_journey_item_load_projection(
              result.split_result
            , result.split_loads
        ));
        MATHFP_TRY(validate_vehicle_journey_item_load_state(result.load_state));
        MATHFP_TRY(validate_capacity_aware_split_diagnostics(result.diagnostics));

        if (result.diagnostics.enabled && result.diagnostics.iterations <= 0) {
            return mathfp::unexpected(
                mathfp::internal_error("enabled capacity-aware split diagnostics must contain at least one iteration")
                    .ctx("iterations", result.diagnostics.iterations)
            );
        }

        if (result.diagnostics.enabled
            && result.diagnostics.iterations > capacity_config.iteration.max_iterations) {
            return mathfp::unexpected(
                mathfp::internal_error("capacity-aware split diagnostics exceed configured max_iterations")
                    .ctx("iterations", result.diagnostics.iterations)
                    .ctx("max_iterations", capacity_config.iteration.max_iterations)
            );
        }

        if (result.diagnostics.enabled && result.diagnostics.converged) {
            const auto satisfies_absolute =
                result.diagnostics.max_load_delta
                    <= capacity_config.iteration.absolute_load_tolerance;
            const auto satisfies_relative =
                result.diagnostics.max_relative_load_delta
                    <= capacity_config.iteration.relative_load_tolerance;

            if (!satisfies_absolute && !satisfies_relative) {
                return mathfp::unexpected(
                    mathfp::internal_error("converged capacity-aware split diagnostics do not satisfy configured tolerances")
                        .ctx("max_load_delta", result.diagnostics.max_load_delta)
                        .ctx("absolute_load_tolerance", capacity_config.iteration.absolute_load_tolerance)
                        .ctx("max_relative_load_delta", result.diagnostics.max_relative_load_delta)
                        .ctx("relative_load_tolerance", capacity_config.iteration.relative_load_tolerance)
                );
            }
        }

        if (result.diagnostics.enabled
            && !result.diagnostics.converged
            && result.diagnostics.iterations != capacity_config.iteration.max_iterations) {
            return mathfp::unexpected(
                mathfp::internal_error("non-converged capacity-aware split must stop only at max_iterations")
                    .ctx("iterations", result.diagnostics.iterations)
                    .ctx("max_iterations", capacity_config.iteration.max_iterations)
            );
        }

        return mathfp::kUnit;
    }

}  // namespace timetable::domain::assignment
