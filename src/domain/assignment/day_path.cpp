#include "timetable/domain/assignment/day_path.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
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

    }  // namespace

    DayPathLeg day_path_leg_of(
        const ConnectionLeg& leg
    ) noexcept {
        return DayPathLeg{
              .kind            = leg.kind
            , .route_segment   = leg.route_segment
            , .physical_from   = leg.physical_from
            , .physical_to     = leg.physical_to
            , .occurrence_from = leg.occurrence_from
            , .occurrence_to   = leg.occurrence_to
            , .line            = leg.line
            , .route           = leg.route
        };
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
        prefix.legs.push_back(std::move(leg));
        return prefix;
    }

    DayPathSignature complete_day_path_signature(
          DayPathPrefix prefix
        , ZoneId        destination
    ) {
        return DayPathSignature{
              .origin      = prefix.origin
            , .destination = destination
            , .legs        = std::move(prefix.legs)
        };
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
        return std::span<const DayPathSupportDescriptor>{
              alternative.support.supports.data()
            , alternative.support.supports.size()
        };
    }

    mathfp::Expected<mathfp::Unit> validate_day_path_alternative(
          const DayPathAlternative& alternative
        , std::size_t               alternative_index
    ) {
        const auto& identity = day_path_signature_of(alternative);
        if (alternative.support.supports.empty()) {
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

        for (std::size_t i = 0; i < alternative.support.supports.size(); ++i) {
            const auto support_signature =
                day_path_signature_of(alternative.support.supports[i].connection);
            if (!(support_signature == identity)) {
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
        auto signature = day_path_signature_of(connection);
        MATHFP_TRY_LET(
              CompleteConnectionMetrics
            , metrics
            , complete_connection_metrics(connection, search_cost, interval)
        );
        const auto connection_metrics = metrics_of(connection);

        auto it = retention.alternatives_by_signature.find(signature);
        if (it == retention.alternatives_by_signature.end()) {
            std::vector<DayPathSupportDescriptor> supports;
            supports.push_back(
                DayPathSupportDescriptor{
                      .connection         = connection
                    , .complete_metrics   = metrics
                    , .connection_metrics = connection_metrics
                }
            );
            auto alternative = DayPathAlternative{
                  .identity = DayPathIdentity{
                      .signature = std::move(signature)
                  }
                , .support = DayPathTimedSupport{
                      .representative                    = std::move(connection)
                    , .representative_metrics            = metrics
                    , .representative_connection_metrics = connection_metrics
                    , .supports                          = std::move(supports)
                  }
            };
            const auto key = alternative.identity.signature;
            retention.alternatives_by_signature.emplace(
                  key
                , std::move(alternative)
            );
            return DayPathRetentionDecision{
                  .inserted_path          = true
                , .replaced_representative = true
                , .timed_connection_count = 1u
            };
        }

        auto& alternative = it->second;
        alternative.support.supports.push_back(
            DayPathSupportDescriptor{
                  .connection         = connection
                , .complete_metrics   = metrics
                , .connection_metrics = connection_metrics
            }
        );
        const auto replaced = better_day_path_representative(
              metrics
            , alternative.support.representative_metrics
        );
        if (replaced) {
            alternative.support.representative =
                alternative.support.supports.back().connection;
            alternative.support.representative_metrics            = metrics;
            alternative.support.representative_connection_metrics = connection_metrics;
        }
        return DayPathRetentionDecision{
              .inserted_path          = false
            , .replaced_representative = replaced
            , .timed_connection_count = alternative.support.supports.size()
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
            DayPathSupportDescriptor{
                  .connection         = connection
                , .complete_metrics   = metrics
                , .connection_metrics = connection_metrics
            }
        );
        return DayPathAlternative{
              .identity = DayPathIdentity{
                  .signature = std::move(signature)
              }
            , .support = DayPathTimedSupport{
                  .representative                    = std::move(connection)
                , .representative_metrics            = metrics
                , .representative_connection_metrics = connection_metrics
                , .supports                          = std::move(supports)
              }
        };
    }

    DayPathMetrics day_path_metrics_of(
        const DayPathAlternative& alternative
    ) noexcept {
        return DayPathMetrics{
              .complete               = alternative.support.representative_metrics
            , .representative         = alternative.support.representative_connection_metrics
            , .timed_connection_count = alternative.support.supports.size()
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
