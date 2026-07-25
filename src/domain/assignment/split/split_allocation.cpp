#include "timetable/domain/assignment/split/split_allocation.hpp"

#include <cmath>
#include <cstdint>
#include <utility>

#include <mathfp/core/error.hpp>
#include <mathfp/core/try.hpp>

#include "timetable/domain/assignment/split/choice_weight.hpp"

namespace timetable::domain::assignment {

    mathfp::Expected<SplitShareAllocationResult> compute_split_share_allocation(
          std::span<const SplitShareAlternativeView> alternatives
        , const TimeInterval&                        interval
        , SplitDemandMass                           demand
        , const SplitShareAllocationPolicy&          policy
    ) {
        if (!std::isfinite(demand.get()) || demand.get() < 0.0) {
            return mathfp::unexpected(
                mathfp::invalid_arg("split share allocation requires finite non-negative demand")
                    .ctx("demand", demand.get())
            );
        }

        if (alternatives.empty()) {
            return SplitShareAllocationResult{};
        }

        std::vector<SplitIndependenceWeight> independences;
        std::vector<SplitRawImpedance>       split_impedances;
        std::vector<SplitLogWeight>          log_weights;
        independences   .reserve(alternatives.size());
        split_impedances.reserve(alternatives.size());
        log_weights     .reserve(alternatives.size());

        for (std::size_t i = 0; i < alternatives.size(); ++i) {
            const auto& alternative = alternatives[i];

            auto raw_impedance_result = compute_split_impedance(
                  alternative.impedance
                , interval
                , policy.impedance
            );
            if (!raw_impedance_result) {
                auto error = std::move(raw_impedance_result.error());
                error.ctx("alternative", static_cast<std::int64_t>(i));
                return mathfp::unexpected(std::move(error));
            }
            const auto raw_impedance = *raw_impedance_result;

            auto choice_impedance_result = apply_impedance_transform(
                  raw_impedance
                , policy.impedance_transform
            );
            if (!choice_impedance_result) {
                auto error = std::move(choice_impedance_result.error());
                error.ctx("alternative"  , static_cast<std::int64_t>(i))
                     .ctx("raw_impedance", raw_impedance.get());
                return mathfp::unexpected(std::move(error));
            }

            auto log_weight_result = split_choice_log_weight(
                  policy.choice_model
                , *choice_impedance_result
                , alternative.independence
            );
            if (!log_weight_result) {
                auto error = std::move(log_weight_result.error());
                error.ctx("alternative", static_cast<std::int64_t>(i));
                return mathfp::unexpected(std::move(error));
            }

            independences   .push_back(alternative.independence);
            split_impedances.push_back(raw_impedance);
            log_weights     .push_back(*log_weight_result);
        }

        MATHFP_TRY_LET(
              SplitAllocation
            , allocation
            , normalize_split_log_weights(
                  std::span<const SplitLogWeight>{ log_weights.data(), log_weights.size() }
                , demand
                , policy.probability
            )
        );

        return SplitShareAllocationResult{
              .independences = std::move(independences)
            , .split_impedances = std::move(split_impedances)
            , .allocation = std::move(allocation)
        };
    }

}  // namespace timetable::domain::assignment
