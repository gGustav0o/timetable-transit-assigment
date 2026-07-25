#include "timetable/domain/assignment/split/split_kernel.hpp"

#include <cmath>
#include <cstdint>
#include <utility>

#include <mathfp/core/error.hpp>
#include <mathfp/core/try.hpp>
#include <mathfp/core/unit.hpp>

namespace timetable::domain::assignment {
    namespace {

        [[nodiscard]] mathfp::Expected<mathfp::Unit> validate_split_kernel_inputs(
              std::span<const SplitLogWeight> log_weights
            , SplitDemandMass                  demand
            , std::span<const std::size_t>    trip_indices
            , std::span<const std::size_t>    stop_indices
            , std::span<const std::size_t>    segment_indices
        ) {
            if (log_weights.empty()) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("split kernel requires at least one alternative")
                );
            }

            if (!std::isfinite(demand.get()) || demand.get() < 0.0) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("demand must be finite non-negative")
                        .ctx("demand", demand.get())
                );
            }

            if (trip_indices.size() != log_weights.size()
                || stop_indices.size() != log_weights.size()
                || segment_indices.size() != log_weights.size()) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("all index spans must match log weights size")
                        .ctx("log_weights", static_cast<std::int64_t>(log_weights.size()))
                        .ctx("trip_indices", static_cast<std::int64_t>(trip_indices.size()))
                        .ctx("stop_indices", static_cast<std::int64_t>(stop_indices.size()))
                        .ctx(
                              "segment_indices"
                            , static_cast<std::int64_t>(segment_indices.size())
                        )
                );
            }

            return mathfp::kUnit;
        }

    }  // namespace

    mathfp::Expected<SplitKernelResult> execute_split_kernel(
          std::span<const SplitLogWeight> log_weights
        , SplitDemandMass                  demand
        , std::span<const std::size_t>    trip_indices
        , std::span<const std::size_t>    stop_indices
        , std::span<const std::size_t>    segment_indices
        , const SplitKernelPolicy&         policy
    ) {
        MATHFP_TRY(validate_split_kernel_inputs(
              log_weights
            , demand
            , trip_indices
            , stop_indices
            , segment_indices
        ));

        MATHFP_TRY_LET(
              SplitAllocation
            , allocation
            , normalize_split_log_weights(log_weights, demand, policy.probability)
        );

        MATHFP_TRY_LET(
              DemandProjectionResult
            , projection_result
            , project_demand(
                  std::span<const SplitProbabilityMass>{
                      allocation.probabilities.data()
                    , allocation.probabilities.size()
                  }
                , demand
                , allocation.residual_index
                , policy.demand_projection
            )
        );

        MATHFP_TRY_LET(
              LoadAccumulationResult
            , load_result
            , accumulate_loads(
                  std::span<const SplitPassengerMass>{
                      projection_result.passengers.data()
                    , projection_result.passengers.size()
                  }
                , trip_indices
                , stop_indices
                , segment_indices
                , policy.load_accumulation
            )
        );

        return SplitKernelResult{
              .probabilities = std::move(allocation.probabilities)
            , .passengers = std::move(projection_result.passengers)
            , .segment_loads = std::move(load_result.segment_loads)
            , .stop_loads = std::move(load_result.stop_loads)
            , .trip_loads = std::move(load_result.trip_loads)
            , .total_alternatives = log_weights.size()
            , .significant_alternatives = projection_result.significant_alternatives
            , .suppressed_alternatives = projection_result.suppressed_alternatives
            , .total_demand = demand.get()
            , .total_load = load_result.total_load
        };
    }

}  // namespace timetable::domain::assignment
