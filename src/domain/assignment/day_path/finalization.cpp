#include "timetable/domain/assignment/day_path/finalization.hpp"

#include <algorithm>
#include <limits>
#include <utility>

namespace timetable::domain::assignment {

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
        std::sort(
              alternatives.begin()
            , alternatives.end()
            , [](const DayPathAlternative& lhs, const DayPathAlternative& rhs) {
                  return lhs.identity.signature < rhs.identity.signature;
              }
        );
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

}  // namespace timetable::domain::assignment
