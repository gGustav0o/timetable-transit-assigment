#include "timetable/domain/assignment/day_path.hpp"

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <limits>
#include <optional>
#include <string>
#include <vector>
#include <utility>

#include <mathfp/core/error.hpp>
#include <mathfp/core/try.hpp>

namespace timetable::domain::assignment {
    namespace {

        [[nodiscard]] bool better_day_path_representative(
              const CompleteConnectionMetrics& candidate
            , const CompleteConnectionMetrics& current
        ) noexcept {
            if (complete_connection_dominates(candidate, current)) {
                return true;
            }
            if (complete_connection_dominates(current, candidate)) {
                return false;
            }

            if (candidate.impedance != current.impedance) {
                return candidate.impedance < current.impedance;
            }
            if (candidate.journey_time.value() != current.journey_time.value()) {
                return candidate.journey_time.value() < current.journey_time.value();
            }
            if (candidate.transfers.get() != current.transfers.get()) {
                return candidate.transfers.get() < current.transfers.get();
            }
            if (candidate.arrival.value() != current.arrival.value()) {
                return candidate.arrival.value() < current.arrival.value();
            }
            return candidate.departure.value() > current.departure.value();
        }

        [[nodiscard]] bool worse_day_path_representative(
              const CompleteConnectionMetrics& candidate
            , const CompleteConnectionMetrics& current
        ) noexcept {
            return better_day_path_representative(current, candidate);
        }

        [[nodiscard]] DayPathRideSupportLeg ride_support_leg_of(
            const ConnectionLeg& leg
        ) {
            return DayPathRideSupportLeg{
                  .connection_segment = *leg.connection_segment
                , .route_segment      = *leg.route_segment
                , .line               = *leg.line
                , .route              = *leg.route
                , .trip               = *leg.trip
                , .occurrence_from    = *leg.occurrence_from
                , .occurrence_to      = *leg.occurrence_to
                , .from_index         = leg.occurrence_from->position
                , .to_index           = leg.occurrence_to->position
                , .departure          = leg.start_time
                , .arrival            = leg.end_time
            };
        }

        [[nodiscard]] std::vector<DayPathRideSupportLeg> ride_support_legs_of(
            const SearchConnection& connection
        ) {
            std::vector<DayPathRideSupportLeg> ride_legs;
            const auto& trace = canonical_connection(connection).trace;
            ride_legs.reserve(trace.legs.size());
            for (const auto& leg : trace.legs) {
                if (is_ride_leg(leg.kind)) {
                    ride_legs.push_back(ride_support_leg_of(leg));
                }
            }
            return ride_legs;
        }

        [[nodiscard]] DayPathSupportDescriptor make_day_path_support_descriptor(
              const SearchConnection&          connection
            , const DayPathSignature&          signature
            , const CompleteConnectionMetrics& complete_metrics
            , const ConnectionMetrics&         connection_metrics
        ) {
            return DayPathSupportDescriptor{
                  .signature           = signature
                , .complete_metrics    = complete_metrics
                , .connection_metrics  = connection_metrics
                , .ride_legs           = ride_support_legs_of(connection)
            };
        }

        [[nodiscard]] std::size_t worst_support_index(
            const std::vector<DayPathSupportDescriptor>& supports
        ) noexcept {
            std::size_t worst = 0;
            for (std::size_t i = 1; i < supports.size(); ++i) {
                if (worse_day_path_representative(
                      supports[i].complete_metrics
                    , supports[worst].complete_metrics
                )) {
                    worst = i;
                }
            }
            return worst;
        }

        [[nodiscard]] bool bounded_day_path_retention(
            const DayPathRetentionConfig& config
        ) noexcept {
            return config.limit_policy
                != DayPathRetentionLimitPolicy::Unbounded;
        }

        [[nodiscard]] bool support_limit_reached(
              const DayPathSplitSupport&    support
            , const DayPathRetentionConfig& config
        ) noexcept {
            return config.max_supports_per_path.has_value()
                && support.supports.size() >= *config.max_supports_per_path;
        }

        [[nodiscard]] bool alternative_limit_exceeded(
              const DayPathRetention&       retention
            , const DayPathRetentionConfig& config
        ) noexcept {
            return config.max_alternatives_per_od.has_value()
                && retention.alternatives_by_signature.size()
                    > *config.max_alternatives_per_od;
        }

        [[nodiscard]] mathfp::Expected<mathfp::Unit> day_path_retention_saturation_error(
              const char*                    subject
            , const DayPathSignature&        signature
            , const DayPathRetentionConfig&  config
        ) {
            return mathfp::unexpected(
                mathfp::internal_error("day-path retention saturated in truth-preserving mode")
                    .ctx("subject", std::string(subject))
                    .ctx("origin", signature.origin.get())
                    .ctx("destination", signature.destination.get())
                    .ctx("limit_policy", std::string(
                        day_path_retention_limit_policy_name(config.limit_policy)
                    ))
            );
        }

        [[nodiscard]] mathfp::Expected<mathfp::Unit> retain_day_path_support(
              DayPathSplitSupport&          support
            , DayPathSupportDescriptor      candidate
            , const DayPathRetentionConfig& config
        ) {
            if (!bounded_day_path_retention(config)
                || !support_limit_reached(support, config)) {
                support.supports.push_back(std::move(candidate));
                return mathfp::kUnit;
            }
            if (support.supports.empty()) {
                return mathfp::kUnit;
            }
            if (config.limit_policy == DayPathRetentionLimitPolicy::FailOnSaturation) {
                return day_path_retention_saturation_error(
                      "support"
                    , candidate.signature
                    , config
                );
            }

            const auto worst = worst_support_index(support.supports);
            if (better_day_path_representative(
                  candidate.complete_metrics
                , support.supports[worst].complete_metrics
            )) {
                support.supports[worst] = std::move(candidate);
            }
            return mathfp::kUnit;
        }

        [[nodiscard]] std::vector<CompleteConnectionMetrics> support_metric_set_of(
            const DayPathAlternative& alternative
        ) {
            std::vector<CompleteConnectionMetrics> metrics;
            metrics.reserve(
                alternative.support.split_support.supports.empty()
                    ? 1u
                    : alternative.support.split_support.supports.size()
            );
            for (const auto& support : alternative.support.split_support.supports) {
                metrics.push_back(support.complete_metrics);
            }
            if (metrics.empty()) {
                metrics.push_back(alternative.support.representative_metrics);
            }
            return metrics;
        }

        [[nodiscard]] CompleteConnectionMetrics best_support_metrics_of(
            const DayPathAlternative& alternative
        ) {
            auto metrics = support_metric_set_of(alternative);
            auto best = metrics.begin();
            for (auto it = std::next(metrics.begin()); it != metrics.end(); ++it) {
                if (better_day_path_representative(*it, *best)) {
                    best = it;
                }
            }
            return *best;
        }

        [[nodiscard]] bool support_set_dominates(
              const DayPathAlternative& lhs
            , const DayPathAlternative& rhs
        ) {
            const auto lhs_metrics = support_metric_set_of(lhs);
            const auto rhs_metrics = support_metric_set_of(rhs);
            return std::all_of(
                  rhs_metrics.begin()
                , rhs_metrics.end()
                , [&](const CompleteConnectionMetrics& rhs_support) {
                      return std::any_of(
                            lhs_metrics.begin()
                          , lhs_metrics.end()
                          , [&](const CompleteConnectionMetrics& lhs_support) {
                                return complete_connection_dominates(
                                      lhs_support
                                    , rhs_support
                                );
                            }
                      );
                  }
            );
        }

        void remove_support_dominated_paths(
            DayPathRetention& retention
        ) {
            for (auto it = retention.alternatives_by_signature.begin();
                 it != retention.alternatives_by_signature.end();) {
                bool dominated = false;
                for (const auto& [other_signature, other] : retention.alternatives_by_signature) {
                    if (other_signature == it->first) {
                        continue;
                    }
                    if (support_set_dominates(other, it->second)) {
                        dominated = true;
                        break;
                    }
                }

                if (dominated) {
                    it = retention.alternatives_by_signature.erase(it);
                } else {
                    ++it;
                }
            }
        }

        [[nodiscard]] auto worst_alternative_iterator(
            DayPathRetention& retention
        ) {
            auto worst = retention.alternatives_by_signature.begin();
            for (auto it = std::next(retention.alternatives_by_signature.begin());
                 it != retention.alternatives_by_signature.end();
                 ++it) {
                if (worse_day_path_representative(
                      best_support_metrics_of(it->second)
                    , best_support_metrics_of(worst->second)
                )) {
                    worst = it;
                }
            }
            return worst;
        }

        [[nodiscard]] mathfp::Expected<mathfp::Unit> enforce_day_path_retention(
              DayPathRetention&             retention
            , const DayPathRetentionConfig& config
        ) {
            remove_support_dominated_paths(retention);
            if (!bounded_day_path_retention(config)
                || !config.max_alternatives_per_od.has_value()) {
                return mathfp::kUnit;
            }
            if (config.limit_policy == DayPathRetentionLimitPolicy::FailOnSaturation
                && alternative_limit_exceeded(retention, config)) {
                const auto& signature = retention.alternatives_by_signature.rbegin()->first;
                return day_path_retention_saturation_error(
                      "alternative"
                    , signature
                    , config
                );
            }
            while (alternative_limit_exceeded(retention, config)) {
                retention.alternatives_by_signature.erase(
                    worst_alternative_iterator(retention)
                );
            }
            return mathfp::kUnit;
        }

    }  // namespace

    DayPathLeg day_path_leg_of(
        const ConnectionLeg& leg
    ) noexcept {
        return production_day_path_leg(DayPathLeg{
              .kind            = leg.kind
            , .route_segment   = leg.route_segment
            , .physical_from   = leg.physical_from
            , .physical_to     = leg.physical_to
            , .occurrence_from = leg.occurrence_from
            , .occurrence_to   = leg.occurrence_to
            , .line            = leg.line
            , .route           = leg.route
        });
    }

    DayPathLeg production_day_path_leg(
        DayPathLeg leg
    ) noexcept {
        leg.route_segment   = std::nullopt;
        leg.occurrence_from = std::nullopt;
        leg.occurrence_to   = std::nullopt;
        if (!is_ride_leg(leg.kind)) {
            leg.line  = std::nullopt;
            leg.route = std::nullopt;
        }
        return leg;
    }

    DayPathPrefix make_day_path_prefix(
        ZoneId origin
    ) {
        return DayPathPrefix{
              .origin = origin
            , .legs   = {}
        };
    }

    DayPathPrefix append_day_path_leg(
          DayPathPrefix prefix
        , DayPathLeg    leg
    ) {
        prefix.legs.push_back(production_day_path_leg(std::move(leg)));
        return prefix;
    }

    DayPathSignature make_day_path_signature_from_tree_label(
          DayPathPrefix prefix
        , ZoneId        destination
    ) {
        for (auto& leg : prefix.legs) {
            leg = production_day_path_leg(std::move(leg));
        }
        return DayPathSignature{
              .origin      = prefix.origin
            , .destination = destination
            , .legs        = std::move(prefix.legs)
        };
    }

    DayPathSignature complete_day_path_signature(
          DayPathPrefix prefix
        , ZoneId        destination
    ) {
        return make_day_path_signature_from_tree_label(
              std::move(prefix)
            , destination
        );
    }

    DayPathSignature day_path_signature_of(
        const SearchConnection& connection
    ) {
        const auto& canonical = canonical_connection(connection);
        DayPathSignature signature{
              .origin      = canonical.origin
            , .destination = canonical.destination
            , .legs        = {}
        };
        signature.legs.reserve(canonical.trace.legs.size());
        for (const auto& leg : canonical.trace.legs) {
            if (is_wait_leg(leg.kind)) {
                continue;
            }
            signature.legs.push_back(day_path_leg_of(leg));
        }
        return signature;
    }

    const DayPathSignature& day_path_signature_of(
        const DayPathAlternative& alternative
    ) noexcept {
        return alternative.identity.signature;
    }

    const DayPathTimedSupport& day_path_support_of(
        const DayPathAlternative& alternative
    ) noexcept {
        return alternative.support;
    }

    const SearchConnection& day_path_representative_connection(
        const DayPathAlternative& alternative
    ) noexcept {
        return alternative.support.representative;
    }

    std::span<const DayPathSupportDescriptor> day_path_support_descriptors(
        const DayPathAlternative& alternative
    ) noexcept {
        return day_path_split_support_descriptors(alternative);
    }

    std::span<const DayPathSupportDescriptor> day_path_split_support_descriptors(
        const DayPathAlternative& alternative
    ) noexcept {
        return std::span<const DayPathSupportDescriptor>{
              alternative.support.split_support.supports.data()
            , alternative.support.split_support.supports.size()
        };
    }

    mathfp::Expected<mathfp::Unit> validate_day_path_alternative(
          const DayPathAlternative& alternative
        , std::size_t               alternative_index
    ) {
        const auto& identity = day_path_signature_of(alternative);
        if (alternative.support.split_support.supports.empty()) {
            return mathfp::unexpected(
                mathfp::internal_error("day-path alternative has empty timed support")
                    .ctx("alternative_index", static_cast<std::int64_t>(alternative_index))
                    .ctx("origin", identity.origin.get())
                    .ctx("destination", identity.destination.get())
            );
        }

        const auto representative_signature =
            day_path_signature_of(alternative.support.representative);
        if (!(representative_signature == identity)) {
            return mathfp::unexpected(
                mathfp::internal_error("day-path representative disagrees with structural identity")
                    .ctx("alternative_index", static_cast<std::int64_t>(alternative_index))
                    .ctx("origin", identity.origin.get())
                    .ctx("destination", identity.destination.get())
            );
        }

        for (std::size_t i = 0; i < alternative.support.split_support.supports.size(); ++i) {
            if (!(alternative.support.split_support.supports[i].signature == identity)) {
                return mathfp::unexpected(
                    mathfp::internal_error("day-path timed support disagrees with structural identity")
                        .ctx("alternative_index", static_cast<std::int64_t>(alternative_index))
                        .ctx("support_index", static_cast<std::int64_t>(i))
                        .ctx("origin", identity.origin.get())
                        .ctx("destination", identity.destination.get())
                );
            }
        }

        return mathfp::kUnit;
    }

    bool day_path_retention_empty(
        const DayPathRetention& retention
    ) noexcept {
        return retention.alternatives_by_signature.empty();
    }

    std::size_t day_path_retention_size(
        const DayPathRetention& retention
    ) noexcept {
        return retention.alternatives_by_signature.size();
    }

    mathfp::Expected<DayPathRetentionDecision> retain_day_path_alternative(
          DayPathRetention&        retention
        , SearchConnection         connection
        , const SearchCostContext& search_cost
        , IntervalId               interval
    ) {
        return retain_day_path_alternative(
              retention
            , std::move(connection)
            , search_cost
            , interval
            , DayPathRetentionConfig{}
        );
    }

    mathfp::Expected<DayPathRetentionDecision> retain_day_path_alternative(
          DayPathRetention&             retention
        , SearchConnection              connection
        , const SearchCostContext&      search_cost
        , IntervalId                    interval
        , const DayPathRetentionConfig& config
    ) {
        return retain_day_path_alternative(
              retention
            , day_path_signature_of(connection)
            , std::move(connection)
            , search_cost
            , interval
            , config
        );
    }

    mathfp::Expected<DayPathRetentionDecision> retain_day_path_alternative(
          DayPathRetention&             retention
        , DayPathSignature              signature
        , SearchConnection              connection
        , const SearchCostContext&      search_cost
        , IntervalId                    interval
        , const DayPathRetentionConfig& config
    ) {
        MATHFP_TRY_LET(
              CompleteConnectionMetrics
            , metrics
            , complete_connection_metrics(connection, search_cost, interval)
        );
        const auto connection_metrics = metrics_of(connection);
        auto support = make_day_path_support_descriptor(
              connection
            , signature
            , metrics
            , connection_metrics
        );

        auto it = retention.alternatives_by_signature.find(signature);
        if (it == retention.alternatives_by_signature.end()) {
            std::vector<DayPathSupportDescriptor> supports;
            supports.push_back(std::move(support));
            auto alternative = DayPathAlternative{
                  .identity = DayPathIdentity{
                      .signature = std::move(signature)
                  }
                , .support = DayPathTimedSupport{
                      .representative                    = std::move(connection)
                    , .representative_metrics            = metrics
                    , .representative_connection_metrics = connection_metrics
                    , .split_support                     = DayPathSplitSupport{
                          .supports = std::move(supports)
                      }
                  }
            };
            const auto key = alternative.identity.signature;
            retention.alternatives_by_signature.emplace(
                  key
                , std::move(alternative)
            );
            MATHFP_TRY(enforce_day_path_retention(retention, config));
            const auto retained =
                retention.alternatives_by_signature.contains(key);
            return DayPathRetentionDecision{
                  .inserted_path          = retained
                , .replaced_representative = retained
                , .timed_connection_count = retained ? 1u : 0u
            };
        }

        auto& alternative = it->second;
        const auto replaced = better_day_path_representative(
              metrics
            , alternative.support.representative_metrics
        );
        MATHFP_TRY(retain_day_path_support(
              alternative.support.split_support
            , std::move(support)
            , config
        ));
        if (replaced) {
            alternative.support.representative = std::move(connection);
            alternative.support.representative_metrics            = metrics;
            alternative.support.representative_connection_metrics = connection_metrics;
        }
        MATHFP_TRY(enforce_day_path_retention(retention, config));
        const auto retained = retention.alternatives_by_signature.find(signature);
        const auto support_count = retained != retention.alternatives_by_signature.end()
            ? retained->second.support.split_support.supports.size()
            : 0u;
        return DayPathRetentionDecision{
            .inserted_path          = false
          , .replaced_representative = replaced && retained != retention.alternatives_by_signature.end()
          , .timed_connection_count = support_count
        };
    }

    mathfp::Expected<DayPathAlternative> make_day_path_alternative(
          SearchConnection         connection
        , const SearchCostContext& search_cost
        , IntervalId               interval
    ) {
        auto signature = day_path_signature_of(connection);
        MATHFP_TRY_LET(
              CompleteConnectionMetrics
            , metrics
            , complete_connection_metrics(connection, search_cost, interval)
        );
        const auto connection_metrics = metrics_of(connection);
        std::vector<DayPathSupportDescriptor> supports;
        supports.push_back(
            make_day_path_support_descriptor(
                  connection
                , signature
                , metrics
                , connection_metrics
            )
        );
        return DayPathAlternative{
              .identity = DayPathIdentity{
                  .signature = std::move(signature)
              }
            , .support = DayPathTimedSupport{
                  .representative                    = std::move(connection)
                , .representative_metrics            = metrics
                , .representative_connection_metrics = connection_metrics
                , .split_support                     = DayPathSplitSupport{
                      .supports = std::move(supports)
                  }
              }
        };
    }

    DayPathMetrics day_path_metrics_of(
        const DayPathAlternative& alternative
    ) noexcept {
        return DayPathMetrics{
              .complete               = alternative.support.representative_metrics
            , .representative         = alternative.support.representative_connection_metrics
            , .timed_connection_count =
                  alternative.support.split_support.supports.size()
        };
    }

    std::vector<SearchConnection> finalize_day_path_representatives(
        DayPathRetention retention
    ) {
        auto alternatives = finalize_day_path_alternatives(std::move(retention));

        std::vector<SearchConnection> representatives;
        representatives.reserve(alternatives.size());
        for (auto& alternative : alternatives) {
            representatives.push_back(std::move(alternative.support.representative));
        }
        return representatives;
    }

    std::vector<DayPathAlternative> finalize_day_path_alternatives(
        DayPathRetention retention
    ) {
        std::vector<DayPathAlternative> alternatives;
        alternatives.reserve(retention.alternatives_by_signature.size());
        for (auto& entry : retention.alternatives_by_signature) {
            alternatives.push_back(std::move(entry.second));
        }
        return alternatives;
    }

    CompleteConnectionMetricSummary summarize_day_path_metrics(
        const DayPathRetention& retention
    ) noexcept {
        CompleteConnectionMetricSummary summary{
              .min_impedance    = std::numeric_limits<double>::infinity()
            , .min_journey_time = std::numeric_limits<double>::infinity()
            , .min_transfers    = std::numeric_limits<double>::infinity()
            , .empty            = retention.alternatives_by_signature.empty()
        };
        for (const auto& [signature, alternative] : retention.alternatives_by_signature) {
            (void)signature;
            summary.min_impedance = std::min(
                  summary.min_impedance
                , alternative.support.representative_metrics.impedance
            );
            summary.min_journey_time = std::min(
                  summary.min_journey_time
                , alternative.support.representative_metrics.journey_time.value()
            );
            summary.min_transfers = std::min(
                  summary.min_transfers
                , static_cast<double>(alternative.support.representative_metrics.transfers.get())
            );
        }
        return summary;
    }

    std::vector<DayPathAlternative> finalize_day_path_alternatives(
          DayPathRetention        retention
        , const ChoiceTolerances& tolerances
        , ChoiceRolloutStage      rollout_stage
    ) {
        if (rollout_stage == ChoiceRolloutStage::ExactOnly) {
            return finalize_day_path_alternatives(std::move(retention));
        }

        const auto summary = summarize_day_path_metrics(retention);
        for (auto it = retention.alternatives_by_signature.begin();
             it != retention.alternatives_by_signature.end();) {
            if (!within_complete_connection_tolerances(
                  it->second.support.representative_metrics
                , summary
                , tolerances
            )) {
                it = retention.alternatives_by_signature.erase(it);
            } else {
                ++it;
            }
        }
        return finalize_day_path_alternatives(std::move(retention));
    }

    std::vector<SearchConnection> finalize_day_path_representatives(
          DayPathRetention        retention
        , const ChoiceTolerances& tolerances
        , ChoiceRolloutStage      rollout_stage
    ) {
        auto alternatives = finalize_day_path_alternatives(
              std::move(retention)
            , tolerances
            , rollout_stage
        );
        std::vector<SearchConnection> representatives;
        representatives.reserve(alternatives.size());
        for (auto& alternative : alternatives) {
            representatives.push_back(std::move(alternative.support.representative));
        }
        return representatives;
    }

    std::vector<SearchConnection> day_path_representative_connections(
        std::span<const DayPathAlternative> alternatives
    ) {
        std::vector<SearchConnection> representatives;
        representatives.reserve(alternatives.size());
        for (const auto& alternative : alternatives) {
            representatives.push_back(alternative.support.representative);
        }
        return representatives;
    }

    mathfp::Expected<std::vector<SearchConnection>>
    retain_day_path_representative_connections(
          std::vector<SearchConnection> connections
        , const SearchCostContext&      search_cost
        , IntervalId                    interval
    ) {
        DayPathRetention retention;
        for (auto& connection : connections) {
            MATHFP_TRY(retain_day_path_alternative(
                  retention
                , std::move(connection)
                , search_cost
                , interval
            ));
        }
        return finalize_day_path_representatives(std::move(retention));
    }

    mathfp::Expected<std::vector<DayPathAlternative>>
    retain_day_path_alternatives(
          std::vector<SearchConnection> connections
        , const SearchCostContext&      search_cost
        , IntervalId                    interval
    ) {
        DayPathRetention retention;
        for (auto& connection : connections) {
            MATHFP_TRY(retain_day_path_alternative(
                  retention
                , std::move(connection)
                , search_cost
                , interval
            ));
        }
        return finalize_day_path_alternatives(std::move(retention));
    }

    mathfp::Expected<std::vector<DayPathAlternative>>
    retain_day_path_alternatives(
          std::vector<SearchConnection> connections
        , const SearchCostContext&      search_cost
        , IntervalId                    interval
        , const ChoiceTolerances&       tolerances
        , ChoiceRolloutStage            rollout_stage
    ) {
        DayPathRetention retention;
        for (auto& connection : connections) {
            MATHFP_TRY(retain_day_path_alternative(
                  retention
                , std::move(connection)
                , search_cost
                , interval
            ));
        }
        return finalize_day_path_alternatives(
              std::move(retention)
            , tolerances
            , rollout_stage
        );
    }

}  // namespace timetable::domain::assignment
