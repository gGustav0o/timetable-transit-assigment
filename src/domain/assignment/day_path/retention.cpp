#include "timetable/domain/assignment/day_path/retention.hpp"

#include <algorithm>
#include <iterator>
#include <string>
#include <utility>

#include <mathfp/core/error.hpp>

namespace timetable::domain::assignment {
namespace {

    [[nodiscard]] bool worse_day_path_representative(
          const CompleteConnectionMetrics& candidate
        , const CompleteConnectionMetrics& current
    ) noexcept {
        return better_day_path_representative(current, candidate);
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
          const char*                   subject
        , const DayPathSignature&       signature
        , const DayPathRetentionConfig& config
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

    void remove_support_dominated_paths(
        DayPathRetention& retention
    ) {
        const auto signatures = sorted_day_path_signatures(retention);
        for (const auto& signature : signatures) {
            const auto candidate_it =
                retention.alternatives_by_signature.find(signature);
            if (candidate_it == retention.alternatives_by_signature.end()) {
                continue;
            }
            bool dominated = false;
            for (const auto& other_signature : signatures) {
                if (other_signature == signature) {
                    continue;
                }
                const auto other_it =
                    retention.alternatives_by_signature.find(other_signature);
                if (other_it == retention.alternatives_by_signature.end()) {
                    continue;
                }
                if (day_path_support_set_dominates(other_it->second, candidate_it->second)) {
                    dominated = true;
                    break;
                }
            }

            if (dominated) {
                retention.alternatives_by_signature.erase(signature);
            }
        }
    }

    [[nodiscard]] DayPathSignature worst_alternative_signature(
        DayPathRetention& retention
    ) {
        auto signatures = sorted_day_path_signatures(retention);
        auto worst = signatures.front();
        for (auto it = std::next(signatures.begin());
             it != signatures.end();
             ++it) {
            const auto current_it =
                retention.alternatives_by_signature.find(*it);
            const auto worst_it =
                retention.alternatives_by_signature.find(worst);
            if (current_it == retention.alternatives_by_signature.end()
                || worst_it == retention.alternatives_by_signature.end()) {
                continue;
            }
            if (worse_day_path_representative(
                  best_day_path_support_metrics(current_it->second)
                , best_day_path_support_metrics(worst_it->second)
            )) {
                worst = *it;
            }
        }
        return worst;
    }

}  // namespace

    bool better_day_path_representative(
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

    mathfp::Expected<mathfp::Unit> retain_day_path_support(
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

    CompleteConnectionMetrics best_day_path_support_metrics(
        const DayPathAlternative& alternative
    ) {
        auto metrics = day_path_support_metric_set(alternative);
        auto best = metrics.begin();
        for (auto it = std::next(metrics.begin()); it != metrics.end(); ++it) {
            if (better_day_path_representative(*it, *best)) {
                best = it;
            }
        }
        return *best;
    }

    std::vector<DayPathSignature> sorted_day_path_signatures(
        const DayPathRetention& retention
    ) {
        std::vector<DayPathSignature> signatures;
        signatures.reserve(retention.alternatives_by_signature.size());
        for (const auto& [signature, _] : retention.alternatives_by_signature) {
            signatures.push_back(signature);
        }
        std::sort(signatures.begin(), signatures.end());
        return signatures;
    }

    mathfp::Expected<mathfp::Unit> enforce_day_path_retention(
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
            const auto signature = sorted_day_path_signatures(retention).back();
            return day_path_retention_saturation_error(
                  "alternative"
                , signature
                , config
            );
        }
        while (alternative_limit_exceeded(retention, config)) {
            retention.alternatives_by_signature.erase(
                worst_alternative_signature(retention)
            );
        }
        return mathfp::kUnit;
    }

}  // namespace timetable::domain::assignment
