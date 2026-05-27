#include "timetable/domain/assignment/skim.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <map>
#include <tuple>
#include <span>
#include <utility>
#include <vector>

#include <mathfp/core/error.hpp>
#include <mathfp/core/summation.hpp>
#include <mathfp/core/try.hpp>

#include "detail/grouping.hpp"

namespace timetable::domain::assignment {
    namespace {

        using DemandKey = detail::grouping::DemandKey;
        using OdKey = detail::grouping::OdKey;
        using ChoiceTaskLookup = std::map<DemandKey, const ChoiceTaskResult*>;
        using DemandLookup = std::map<DemandKey, const DemandEntry*>;
        using OdDayChoiceLookup = std::map<OdKey, const OdDayPathChoicePairResult*>;
        using TaskConnectionTraceMap = std::map<DemandKey, std::map<detail::grouping::ConnectionTraceKey, bool>>;
        using SkimEntryKey = std::tuple<std::int64_t, std::int64_t, std::int64_t>;

        struct SkimAlternative final {
            const ConnectionDemandShare* share{};
            ConnectionMetrics            metrics{};
            double                       weight{};
        };

        [[nodiscard]] bool finite_nonnegative(
            double value
        ) noexcept {
            return std::isfinite(value) && value >= 0.0;
        }

        [[nodiscard]] bool finite_nonnegative_time(
            Time value
        ) noexcept {
            return finite_nonnegative(value.value());
        }

        [[nodiscard]] bool numerically_zero(
            double value
        ) noexcept {
            return value == 0.0;
        }

        [[nodiscard]] SkimEntryKey skim_entry_key(
            const AssignmentSkimEntry& entry
        ) noexcept {
            return SkimEntryKey{
                  entry.origin.get()
                , entry.destination.get()
                , entry.interval.get()
            };
        }

        [[nodiscard]] mathfp::Expected<const TimeInterval*> find_input_interval(
              const InputModel& input
            , IntervalId        interval_id
        ) {
            for (const auto& interval : input.intervals) {
                if (interval.id == interval_id) {
                    return &interval;
                }
            }
            return mathfp::unexpected(
                mathfp::invalid_arg("skim input: demand references unknown interval")
                    .ctx("interval_id", interval_id.get())
            );
        }

        [[nodiscard]] mathfp::Expected<mathfp::Unit> validate_weighted_value(
              const SkimWeightedValue& value
            , std::size_t              index
        ) {
            if (!std::isfinite(value.value)) {
                return mathfp::unexpected(
                    mathfp::domain_error("skim aggregation value must be finite")
                        .ctx("index", static_cast<std::int64_t>(index))
                        .ctx("value", value.value)
                );
            }
            if (!finite_nonnegative(value.weight)) {
                return mathfp::unexpected(
                    mathfp::domain_error("skim aggregation weight must be finite and non-negative")
                        .ctx("index" , static_cast<std::int64_t>(index))
                        .ctx("weight", value.weight)
                );
            }
            return mathfp::kUnit;
        }

        [[nodiscard]] mathfp::Expected<double> weighted_mean(
            std::span<const SkimWeightedValue> values
        ) {
            mathfp::CompensatedSum<double> numerator;
            mathfp::CompensatedSum<double> denominator;
            for (std::size_t i = 0; i < values.size(); ++i) {
                MATHFP_TRY(validate_weighted_value(values[i], i));
                if (!(values[i].weight > 0.0)) {
                    continue;
                }
                numerator.add(values[i].value * values[i].weight);
                denominator.add(values[i].weight);
            }

            const auto weight_sum = denominator.value();
            if (!(weight_sum > 0.0)) {
                return 0.0;
            }
            return numerator.value() / weight_sum;
        }

        [[nodiscard]] mathfp::Expected<double> weighted_quantile(
              std::span<const SkimWeightedValue> values
            , double                            quantile
        ) {
            std::vector<SkimWeightedValue> positive_values;
            positive_values.reserve(values.size());

            mathfp::CompensatedSum<double> total_weight;
            for (std::size_t i = 0; i < values.size(); ++i) {
                MATHFP_TRY(validate_weighted_value(values[i], i));
                if (!(values[i].weight > 0.0)) {
                    continue;
                }
                positive_values.push_back(values[i]);
                total_weight.add(values[i].weight);
            }

            if (positive_values.empty()) {
                return 0.0;
            }

            std::stable_sort(
                  positive_values.begin()
                , positive_values.end()
                , [](const SkimWeightedValue& lhs, const SkimWeightedValue& rhs) {
                      return lhs.value < rhs.value;
                  }
            );

            if (quantile <= 0.0) {
                return positive_values.front().value;
            }
            if (quantile >= 1.0) {
                return positive_values.back().value;
            }

            const auto target = quantile * total_weight.value();
            mathfp::CompensatedSum<double> prefix_weight;
            for (const auto& value : positive_values) {
                prefix_weight.add(value.weight);
                if (prefix_weight.value() >= target) {
                    return value.value;
                }
            }
            return positive_values.back().value;
        }

        [[nodiscard]] mathfp::Expected<ChoiceTaskLookup> build_strict_choice_task_lookup(
            const ConnectionChoiceResult& choice_result
        ) {
            ChoiceTaskLookup lookup;
            for (const auto& task_result : choice_result.task_results) {
                const auto key = DemandKey{
                      .origin      = task_result.task.origin
                    , .destination = task_result.task.destination
                    , .interval    = task_result.task.interval.id
                };
                if (!lookup.emplace(key, &task_result).second) {
                    return mathfp::unexpected(
                        mathfp::internal_error("skim input: choice result contains duplicate task result")
                            .ctx("origin"     , key.origin.get())
                            .ctx("destination", key.destination.get())
                            .ctx("interval_id", key.interval.get())
                    );
                }
            }
            return lookup;
        }

        [[nodiscard]] mathfp::Expected<DemandLookup> build_demand_lookup(
            const InputModel& input
        ) {
            DemandLookup lookup;
            for (const auto& demand : input.demand) {
                const auto key = detail::grouping::demand_key(demand);
                if (!lookup.emplace(key, &demand).second) {
                    return mathfp::unexpected(
                        mathfp::invalid_arg("skim input: duplicate demand key")
                            .ctx("origin"     , key.origin.get())
                            .ctx("destination", key.destination.get())
                            .ctx("interval_id", key.interval.get())
                    );
                }
            }
            return lookup;
        }

        [[nodiscard]] mathfp::Expected<OdDayChoiceLookup> build_od_day_choice_lookup(
            const OdDayPathChoiceResult& choice_result
        ) {
            OdDayChoiceLookup lookup;
            for (const auto& origin_result : choice_result.origin_results) {
                for (const auto& pair_result : origin_result.pair_results) {
                    const auto key = OdKey{
                          .origin      = pair_result.origin
                        , .destination = pair_result.destination
                    };
                    if (!lookup.emplace(key, &pair_result).second) {
                        return mathfp::unexpected(
                            mathfp::internal_error("skim input: OD-day choice result contains duplicate OD pair")
                                .ctx("origin"     , key.origin.get())
                                .ctx("destination", key.destination.get())
                        );
                    }
                }
            }
            return lookup;
        }

        [[nodiscard]] std::vector<SearchConnection> admissible_od_day_connections(
              const OdDayPathChoicePairResult&  pair_result
            , const TimeInterval&               interval
            , const AssignmentPeriodConfig&     assignment_period
            , const ConnectionAdmissibilityConfig& admissibility_config
        ) {
            std::vector<SearchConnection> connections;
            connections.reserve(pair_result.connections.size());
            for (const auto& connection : pair_result.connections) {
                if (connection_admissible_for_demand_segment(
                      metrics_of(connection)
                    , interval
                    , assignment_period
                    , admissibility_config
                )) {
                    connections.push_back(connection);
                }
            }
            return connections;
        }

        [[nodiscard]] mathfp::Expected<ConnectionChoiceResult> make_interval_choice_projection(
              const OdDayPathChoiceResult&       choice_result
            , const InputModel&                  input
            , const AssignmentPeriodConfig&      assignment_period
            , const ConnectionAdmissibilityConfig& admissibility_config
        ) {
            MATHFP_TRY_LET(OdDayChoiceLookup, choice_lookup, build_od_day_choice_lookup(choice_result));

            ConnectionChoiceResult projection{
                  .connections  = choice_result.connections
                , .task_results = {}
            };
            projection.task_results.reserve(input.demand.size());

            for (std::size_t i = 0; i < input.demand.size(); ++i) {
                const auto& demand = input.demand[i];
                MATHFP_TRY_LET(const TimeInterval*, interval, find_input_interval(input, demand.interval));
                ChoiceTaskResult task_result{
                      .task = SearchTask{
                          .index            = SearchTaskRef{ static_cast<std::int64_t>(i) }
                        , .origin           = demand.origin
                        , .destination      = demand.destination
                        , .interval         = *interval
                        , .departure_domain = {}
                      }
                    , .connections = {}
                };

                const auto choice_it = choice_lookup.find(detail::grouping::od_key(demand));
                if (choice_it != choice_lookup.end()) {
                    task_result.connections = admissible_od_day_connections(
                          *choice_it->second
                        , *interval
                        , assignment_period
                        , admissibility_config
                    );
                }
                projection.task_results.push_back(std::move(task_result));
            }

            return projection;
        }

        [[nodiscard]] TaskConnectionTraceMap build_task_connection_trace_map(
            const ConnectionChoiceResult& choice_result
        ) {
            TaskConnectionTraceMap traces;
            for (const auto& task_result : choice_result.task_results) {
                auto& task_traces = traces[DemandKey{
                      .origin      = task_result.task.origin
                    , .destination = task_result.task.destination
                    , .interval    = task_result.task.interval.id
                }];
                for (const auto& connection : task_result.connections) {
                    task_traces[detail::grouping::connection_trace_key(connection)] = true;
                }
            }
            return traces;
        }

        [[nodiscard]] mathfp::Expected<mathfp::Unit> validate_split_shares_are_task_local(
              const ConnectionChoiceResult& choice_result
            , const DemandSplitResult&      split_result
        ) {
            const auto task_traces = build_task_connection_trace_map(choice_result);
            for (std::size_t i = 0; i < split_result.shares.size(); ++i) {
                const auto& share = split_result.shares[i];
                const auto key = detail::grouping::demand_key(share);
                const auto task_it = task_traces.find(key);
                if (task_it == task_traces.end()) {
                    return mathfp::unexpected(
                        mathfp::internal_error("skim input: split share has no matching choice task")
                            .ctx("share_index", static_cast<std::int64_t>(i))
                            .ctx("origin"     , share.origin.get())
                            .ctx("destination", share.destination.get())
                            .ctx("interval_id", share.interval.get())
                    );
                }
                if (!task_it->second.contains(detail::grouping::connection_trace_key(share.connection))) {
                    return mathfp::unexpected(
                        mathfp::internal_error("skim input: split share connection is outside its choice task")
                            .ctx("share_index", static_cast<std::int64_t>(i))
                            .ctx("origin"     , share.origin.get())
                            .ctx("destination", share.destination.get())
                            .ctx("interval_id", share.interval.get())
                    );
                }
            }
            return mathfp::kUnit;
        }

        [[nodiscard]] mathfp::Expected<mathfp::Unit> validate_shares_reference_input_demand(
              const DemandLookup&      demands
            , const DemandSplitResult& split_result
        ) {
            for (std::size_t i = 0; i < split_result.shares.size(); ++i) {
                const auto& share = split_result.shares[i];
                const auto key = detail::grouping::demand_key(share);
                if (!demands.contains(key)) {
                    return mathfp::unexpected(
                        mathfp::internal_error("skim input: split share references unknown demand key")
                            .ctx("share_index", static_cast<std::int64_t>(i))
                            .ctx("origin"     , share.origin.get())
                            .ctx("destination", share.destination.get())
                            .ctx("interval_id", share.interval.get())
                    );
                }
            }
            return mathfp::kUnit;
        }

        [[nodiscard]] mathfp::Expected<SkimAlternative> make_skim_alternative(
              const ConnectionDemandShare& share
            , const SkimMatrixConfig&      config
            , std::size_t                  share_index
        ) {
            if (!finite_nonnegative(share.passengers)
                || !finite_nonnegative(share.probability)
                || !finite_nonnegative(share.independence)
                || !std::isfinite(share.split_impedance)) {
                return mathfp::unexpected(
                    mathfp::domain_error("skim input: split share contains invalid scalar")
                        .ctx("share_index"     , static_cast<std::int64_t>(share_index))
                        .ctx("passengers"      , share.passengers)
                        .ctx("probability"     , share.probability)
                        .ctx("independence"    , share.independence)
                        .ctx("split_impedance" , share.split_impedance)
                );
            }

            const auto weight = config.volume_weighted ? share.passengers : 1.0;
            return SkimAlternative{
                  .share   = &share
                , .metrics = metrics_of(share.connection)
                , .weight  = weight
            };
        }

        [[nodiscard]] mathfp::Expected<std::vector<SkimAlternative>> derive_skim_alternatives(
              const std::vector<const ConnectionDemandShare*>& shares
            , const SkimMatrixConfig&                          config
        ) {
            std::vector<SkimAlternative> alternatives;
            alternatives.reserve(shares.size());
            for (std::size_t i = 0; i < shares.size(); ++i) {
                MATHFP_TRY_LET(
                      SkimAlternative
                    , alternative
                    , make_skim_alternative(*shares[i], config, i)
                );
                if (!(alternative.weight > 0.0)) {
                    continue;
                }
                alternatives.push_back(std::move(alternative));
            }
            return alternatives;
        }

        [[nodiscard]] mathfp::Expected<std::vector<SkimAlternative>> take_low_impedance_prefix(
              std::vector<SkimAlternative> alternatives
            , double                       share
        ) {
            MATHFP_TRY_LET(
                  std::size_t
                , prefix_count
                , low_impedance_connection_count(alternatives.size(), share)
            );
            if (prefix_count == 0) {
                return std::vector<SkimAlternative>{};
            }

            std::stable_sort(
                  alternatives.begin()
                , alternatives.end()
                , [](const SkimAlternative& lhs, const SkimAlternative& rhs) {
                      return lhs.share->split_impedance < rhs.share->split_impedance;
                  }
            );

            if (prefix_count >= alternatives.size()) {
                return alternatives;
            }

            std::vector<SkimAlternative> prefix;
            prefix.reserve(prefix_count);

            for (std::size_t i = 0; i < prefix_count; ++i) {
                auto& alternative = alternatives[i];
                prefix.push_back(std::move(alternative));
            }
            return prefix;
        }

        [[nodiscard]] std::vector<SkimWeightedValue> metric_values(
              std::span<const SkimAlternative> alternatives
            , double (*project)(const SkimAlternative&)
        ) {
            std::vector<SkimWeightedValue> values;
            values.reserve(alternatives.size());
            for (const auto& alternative : alternatives) {
                values.push_back(
                    SkimWeightedValue{
                          .value  = project(alternative)
                        , .weight = alternative.weight
                    }
                );
            }
            return values;
        }

        [[nodiscard]] mathfp::Expected<double> aggregate_metric(
              std::span<const SkimAlternative> alternatives
            , const SkimMatrixConfig&          config
            , double (*project)(const SkimAlternative&)
        ) {
            const auto values = metric_values(alternatives, project);
            return aggregate_skim_values(
                  std::span<const SkimWeightedValue>{ values.data(), values.size() }
                , config
            );
        }

        [[nodiscard]] mathfp::Expected<Time> aggregate_time_metric(
              std::span<const SkimAlternative> alternatives
            , const SkimMatrixConfig&          config
            , double (*project)(const SkimAlternative&)
        ) {
            MATHFP_TRY_LET(double, value, aggregate_metric(alternatives, config, project));
            return Time{ value };
        }

        [[nodiscard]] double journey_time_value(
            const SkimAlternative& alternative
        ) noexcept {
            return alternative.metrics.journey_time.value();
        }

        [[nodiscard]] double in_vehicle_time_value(
            const SkimAlternative& alternative
        ) noexcept {
            return alternative.metrics.in_vehicle_time.value();
        }

        [[nodiscard]] double access_time_value(
            const SkimAlternative& alternative
        ) noexcept {
            return alternative.metrics.access_time.value();
        }

        [[nodiscard]] double egress_time_value(
            const SkimAlternative& alternative
        ) noexcept {
            return alternative.metrics.egress_time.value();
        }

        [[nodiscard]] double walk_time_value(
            const SkimAlternative& alternative
        ) noexcept {
            return alternative.metrics.walk_time.value();
        }

        [[nodiscard]] double wait_time_value(
            const SkimAlternative& alternative
        ) noexcept {
            return alternative.metrics.wait_time.value();
        }

        [[nodiscard]] double transfer_wait_time_value(
            const SkimAlternative& alternative
        ) noexcept {
            return alternative.metrics.transfer_wait_time.value();
        }

        [[nodiscard]] double transfer_walk_time_value(
            const SkimAlternative& alternative
        ) noexcept {
            return alternative.metrics.transfer_walk_time.value();
        }

        [[nodiscard]] double transfers_value(
            const SkimAlternative& alternative
        ) noexcept {
            return static_cast<double>(alternative.metrics.transfer_count.get());
        }

        [[nodiscard]] double fare_value(
            const SkimAlternative& alternative
        ) noexcept {
            return alternative.metrics.fare;
        }

        [[nodiscard]] double split_impedance_value(
            const SkimAlternative& alternative
        ) noexcept {
            return alternative.share->split_impedance;
        }

        [[nodiscard]] double assigned_passenger_sum(
            const std::vector<const ConnectionDemandShare*>& shares
        ) {
            return mathfp::compensated_sum_by(shares, [](const ConnectionDemandShare* share) {
                return share->passengers;
            });
        }

        [[nodiscard]] mathfp::Expected<AssignmentSkimEntry> build_skim_entry(
              const DemandEntry&                  demand
            , const ChoiceTaskLookup&             tasks
            , const detail::grouping::ShareGroups& shares_by_key
            , const SkimMatrixConfig&             config
        ) {
            if (!finite_nonnegative(demand.passengers)) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("skim input: demand passengers must be finite and non-negative")
                        .ctx("origin"     , demand.origin.get())
                        .ctx("destination", demand.destination.get())
                        .ctx("interval_id", demand.interval.get())
                        .ctx("passengers" , demand.passengers)
                );
            }

            const auto key = detail::grouping::demand_key(demand);
            const auto task_it = tasks.find(key);
            const auto shares_it = shares_by_key.find(key);

            const auto connection_count = task_it == tasks.end()
                ? std::size_t{ 0 }
                : task_it->second->connections.size();

            if (demand.passengers > 0.0 && task_it == tasks.end()) {
                return mathfp::unexpected(
                    mathfp::internal_error("skim input: positive demand entry has no matching choice task")
                        .ctx("origin"     , demand.origin.get())
                        .ctx("destination", demand.destination.get())
                        .ctx("interval_id", demand.interval.get())
                );
            }

            const std::vector<const ConnectionDemandShare*> empty_shares;
            const auto& shares = shares_it == shares_by_key.end() ? empty_shares : shares_it->second;
            MATHFP_TRY_LET(
                  std::vector<SkimAlternative>
                , all_alternatives
                , derive_skim_alternatives(shares, config)
            );
            MATHFP_TRY_LET(
                  std::vector<SkimAlternative>
                , included_alternatives
                , take_low_impedance_prefix(
                  std::move(all_alternatives)
                , config.low_impedance_connection_share
                )
            );
            const auto included_span = std::span<const SkimAlternative>{
                  included_alternatives.data()
                , included_alternatives.size()
            };

            AssignmentSkimEntry entry{
                  .origin                    = demand.origin
                , .destination               = demand.destination
                , .interval                  = demand.interval
                , .demand_passengers         = demand.passengers
                , .assigned_passengers       = assigned_passenger_sum(shares)
                , .connection_count          = connection_count
                , .included_connection_count = included_alternatives.size()
            };

            MATHFP_TRY_LET(Time, journey_time, aggregate_time_metric(
                  included_span
                , config
                , journey_time_value
            ));
            MATHFP_TRY_LET(Time, in_vehicle_time, aggregate_time_metric(
                  included_span
                , config
                , in_vehicle_time_value
            ));
            MATHFP_TRY_LET(Time, access_time, aggregate_time_metric(
                  included_span
                , config
                , access_time_value
            ));
            MATHFP_TRY_LET(Time, egress_time, aggregate_time_metric(
                  included_span
                , config
                , egress_time_value
            ));
            MATHFP_TRY_LET(Time, walk_time, aggregate_time_metric(
                  included_span
                , config
                , walk_time_value
            ));
            MATHFP_TRY_LET(Time, wait_time, aggregate_time_metric(
                  included_span
                , config
                , wait_time_value
            ));
            MATHFP_TRY_LET(Time, transfer_wait_time, aggregate_time_metric(
                  included_span
                , config
                , transfer_wait_time_value
            ));
            MATHFP_TRY_LET(Time, transfer_walk_time, aggregate_time_metric(
                  included_span
                , config
                , transfer_walk_time_value
            ));
            MATHFP_TRY_LET(double, transfers, aggregate_metric(
                  included_span
                , config
                , transfers_value
            ));
            MATHFP_TRY_LET(double, fare, aggregate_metric(
                  included_span
                , config
                , fare_value
            ));
            MATHFP_TRY_LET(double, split_impedance, aggregate_metric(
                  included_span
                , config
                , split_impedance_value
            ));

            entry.journey_time       = journey_time;
            entry.in_vehicle_time    = in_vehicle_time;
            entry.access_time        = access_time;
            entry.egress_time        = egress_time;
            entry.walk_time          = walk_time;
            entry.wait_time          = wait_time;
            entry.transfer_wait_time = transfer_wait_time;
            entry.transfer_walk_time = transfer_walk_time;
            entry.transfers          = transfers;
            entry.fare               = fare;
            entry.split_impedance    = split_impedance;

            return entry;
        }

    }  // namespace

    mathfp::Expected<mathfp::Unit> validate_assignment_skim_matrix(
        const AssignmentSkimMatrix& skim_matrix
    ) {
        switch (skim_matrix.status) {
            case AssignmentSkimMatrixStatus::DisabledByConfig:
            case AssignmentSkimMatrixStatus::Calculated:
            case AssignmentSkimMatrixStatus::SkippedAssignmentDisabled:
                break;
            default:
                return mathfp::unexpected(
                    mathfp::internal_error("assignment skim matrix status is unsupported")
                );
        }

        if (skim_matrix.status != AssignmentSkimMatrixStatus::Calculated
            && !skim_matrix.entries.empty()) {
            return mathfp::unexpected(
                mathfp::internal_error("non-calculated assignment skim matrix must not contain entries")
                    .ctx("entry_count", static_cast<std::int64_t>(skim_matrix.entries.size()))
            );
        }

        std::map<SkimEntryKey, bool> keys;
        for (std::size_t i = 0; i < skim_matrix.entries.size(); ++i) {
            const auto& entry = skim_matrix.entries[i];
            if (!keys.emplace(skim_entry_key(entry), true).second) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("skim matrix contains duplicate OD-interval entry")
                        .ctx("skim_index" , static_cast<std::int64_t>(i))
                        .ctx("origin"     , entry.origin.get())
                        .ctx("destination", entry.destination.get())
                        .ctx("interval_id", entry.interval.get())
                );
            }

            if (
                   !finite_nonnegative(entry.demand_passengers)
                || !finite_nonnegative(entry.assigned_passengers)
                || !finite_nonnegative_time(entry.journey_time)
                || !finite_nonnegative_time(entry.in_vehicle_time)
                || !finite_nonnegative_time(entry.access_time)
                || !finite_nonnegative_time(entry.egress_time)
                || !finite_nonnegative_time(entry.walk_time)
                || !finite_nonnegative_time(entry.wait_time)
                || !finite_nonnegative_time(entry.transfer_wait_time)
                || !finite_nonnegative_time(entry.transfer_walk_time)
                || !finite_nonnegative(entry.transfers)
                || !finite_nonnegative(entry.fare)
                || !finite_nonnegative(entry.split_impedance)
                || entry.included_connection_count > entry.connection_count
            ) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("skim matrix contains invalid entry")
                        .ctx("skim_index"               , static_cast<std::int64_t>(i))
                        .ctx("origin"                   , entry.origin.get())
                        .ctx("destination"              , entry.destination.get())
                        .ctx("interval_id"              , entry.interval.get())
                        .ctx("demand_passengers"        , entry.demand_passengers)
                        .ctx("assigned_passengers"      , entry.assigned_passengers)
                        .ctx("connection_count"         , static_cast<std::int64_t>(entry.connection_count))
                        .ctx("included_connection_count", static_cast<std::int64_t>(entry.included_connection_count))
                );
            }

            if (entry.included_connection_count == 0
                && (
                       !numerically_zero(entry.journey_time.value())
                    || !numerically_zero(entry.in_vehicle_time.value())
                    || !numerically_zero(entry.access_time.value())
                    || !numerically_zero(entry.egress_time.value())
                    || !numerically_zero(entry.walk_time.value())
                    || !numerically_zero(entry.wait_time.value())
                    || !numerically_zero(entry.transfer_wait_time.value())
                    || !numerically_zero(entry.transfer_walk_time.value())
                    || !numerically_zero(entry.transfers)
                    || !numerically_zero(entry.fare)
                    || !numerically_zero(entry.split_impedance)
                )) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("empty skim entry must have zero aggregate attributes")
                        .ctx("skim_index" , static_cast<std::int64_t>(i))
                        .ctx("origin"     , entry.origin.get())
                        .ctx("destination", entry.destination.get())
                        .ctx("interval_id", entry.interval.get())
                );
            }
        }

        return mathfp::kUnit;
    }

    mathfp::Expected<double> aggregate_skim_values(
          std::span<const SkimWeightedValue> values
        , const SkimMatrixConfig&            config
    ) {
        MATHFP_TRY(validate_skim_matrix_config(config));
        switch (config.func) {
            case SkimAggregationFunc::Mean:
                return weighted_mean(values);
            case SkimAggregationFunc::Quantile:
                return weighted_quantile(values, config.quantile);
        }
        return mathfp::unexpected(
            mathfp::internal_error("unsupported skim aggregation function")
        );
    }

    mathfp::Expected<std::size_t> low_impedance_connection_count(
          std::size_t connection_count
        , double      low_impedance_connection_share
    ) {
        if (!std::isfinite(low_impedance_connection_share)
            || low_impedance_connection_share < 0.0
            || low_impedance_connection_share > 1.0) {
            return mathfp::unexpected(
                mathfp::invalid_arg("skim low impedance connection share must be finite and in [0, 1]")
                    .ctx("low_impedance_connection_share", low_impedance_connection_share)
            );
        }
        if (connection_count == 0 || low_impedance_connection_share <= 0.0) {
            return std::size_t{ 0 };
        }

        const auto requested = static_cast<std::size_t>(
            std::ceil(low_impedance_connection_share * static_cast<double>(connection_count))
        );
        return std::clamp(requested, std::size_t{ 1 }, connection_count);
    }

    mathfp::Expected<AssignmentSkimMatrix> build_assignment_skim_matrix(
          const ConnectionChoiceResult& choice_result
        , const InputModel&             input
        , const DemandSplitResult&      split_result
        , const SkimMatrixConfig&       config
    ) {
        MATHFP_TRY(validate_skim_matrix_config(config));
        if (!config.enabled) {
            return AssignmentSkimMatrix{
                .status = AssignmentSkimMatrixStatus::DisabledByConfig
            };
        }

        MATHFP_TRY(validate_split_shares_are_task_local(choice_result, split_result));
        MATHFP_TRY_LET(DemandLookup, demand_lookup, build_demand_lookup(input));
        MATHFP_TRY(validate_shares_reference_input_demand(demand_lookup, split_result));
        MATHFP_TRY_LET(ChoiceTaskLookup, tasks, build_strict_choice_task_lookup(choice_result));

        const auto shares_by_key = detail::grouping::group_shares_by_demand_key(split_result.shares);

        AssignmentSkimMatrix matrix{
            .status = AssignmentSkimMatrixStatus::Calculated
        };
        matrix.entries.reserve(input.demand.size());
        for (const auto& demand : input.demand) {
            MATHFP_TRY_LET(
                  AssignmentSkimEntry
                , entry
                , build_skim_entry(
                      demand
                    , tasks
                    , shares_by_key
                    , config
                )
            );
            matrix.entries.push_back(std::move(entry));
        }

        MATHFP_TRY(validate_assignment_skim_matrix(matrix));
        return matrix;
    }

    mathfp::Expected<AssignmentSkimMatrix> build_assignment_skim_matrix(
          const OdDayPathChoiceResult&       choice_result
        , const InputModel&                  input
        , const DemandSplitResult&           split_result
        , const AssignmentPeriodConfig&      assignment_period
        , const ConnectionAdmissibilityConfig& admissibility_config
        , const SkimMatrixConfig&            config
    ) {
        MATHFP_TRY(validate_skim_matrix_config(config));
        if (!config.enabled) {
            return AssignmentSkimMatrix{
                .status = AssignmentSkimMatrixStatus::DisabledByConfig
            };
        }

        MATHFP_TRY_LET(
              ConnectionChoiceResult
            , interval_choice_projection
            , make_interval_choice_projection(
                  choice_result
                , input
                , assignment_period
                , admissibility_config
            )
        );
        return build_assignment_skim_matrix(
              interval_choice_projection
            , input
            , split_result
            , config
        );
    }

}  // namespace timetable::domain::assignment
