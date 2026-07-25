#include "timetable/domain/assignment/split/load_accumulation.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

#include <mathfp/core/error.hpp>
#include <mathfp/core/summation.hpp>
#include <mathfp/core/try.hpp>

namespace timetable::domain::assignment {
    namespace {

        [[nodiscard]] mathfp::Expected<mathfp::Unit> validate_load_accumulation_policy(
            const LoadAccumulationPolicy& policy
        ) {
            if (!std::isfinite(policy.load_accumulation_threshold)
                || policy.load_accumulation_threshold < 0.0) {
                return mathfp::unexpected(
                    mathfp::invalid_arg(
                        "load accumulation threshold must be finite and non-negative"
                    )
                        .ctx(
                              "load_accumulation_threshold"
                            , policy.load_accumulation_threshold
                        )
                );
            }
            return mathfp::kUnit;
        }

        [[nodiscard]] bool is_significant_load(
              double load
            , double threshold
        ) noexcept {
            return load > threshold;
        }

        [[nodiscard]] mathfp::Expected<mathfp::Unit> validate_matching_indices(
              std::span<const SplitPassengerMass> passengers
            , std::span<const std::size_t>       trip_indices
            , std::span<const std::size_t>       stop_indices
            , std::span<const std::size_t>       segment_indices
        ) {
            if (passengers.empty()) {
                return mathfp::unexpected(
                    mathfp::invalid_arg(
                        "load accumulation requires at least one passenger flow"
                    )
                );
            }

            if (trip_indices.size() != passengers.size()
                || stop_indices.size() != passengers.size()
                || segment_indices.size() != passengers.size()) {
                return mathfp::unexpected(
                    mathfp::invalid_arg(
                        "load accumulation requires equal size index spans"
                    )
                        .ctx("passengers", static_cast<std::int64_t>(passengers.size()))
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

        [[nodiscard]] mathfp::Expected<mathfp::Unit> validate_passengers(
            std::span<const SplitPassengerMass> passengers
        ) {
            for (std::size_t i = 0; i < passengers.size(); ++i) {
                const auto passenger_count = passengers[i].get();
                if (!std::isfinite(passenger_count) || passenger_count < 0.0) {
                    return mathfp::unexpected(
                        mathfp::domain_error(
                            "passenger count must be finite and non-negative"
                        )
                            .ctx("index", static_cast<std::int64_t>(i))
                            .ctx("passenger_count", passenger_count)
                    );
                }
            }
            return mathfp::kUnit;
        }

        [[nodiscard]] mathfp::Expected<std::size_t> load_vector_size(
            std::span<const std::size_t> indices
        ) {
            const auto max_index = *std::max_element(indices.begin(), indices.end());
            if (max_index == std::numeric_limits<std::size_t>::max()) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("load index is too large")
                );
            }
            return max_index + 1u;
        }

        [[nodiscard]] std::size_t significant_load_count(
              std::span<const double> loads
            , double                  threshold
        ) noexcept {
            std::size_t count = 0;
            for (const auto load : loads) {
                if (is_significant_load(load, threshold)) {
                    ++count;
                }
            }
            return count;
        }

    }  // namespace

    mathfp::Expected<LoadAccumulationResult> accumulate_loads(
          std::span<const SplitPassengerMass> passenger_passengers
        , std::span<const std::size_t>       trip_indices
        , std::span<const std::size_t>       stop_indices
        , std::span<const std::size_t>       segment_indices
        , const LoadAccumulationPolicy&       policy
    ) {
        MATHFP_TRY(validate_load_accumulation_policy(policy));
        MATHFP_TRY(validate_matching_indices(
              passenger_passengers
            , trip_indices
            , stop_indices
            , segment_indices
        ));
        MATHFP_TRY(validate_passengers(passenger_passengers));

        MATHFP_TRY_LET(std::size_t, trip_load_count, load_vector_size(trip_indices));
        MATHFP_TRY_LET(std::size_t, stop_load_count, load_vector_size(stop_indices));
        MATHFP_TRY_LET(
              std::size_t
            , segment_load_count
            , load_vector_size(segment_indices)
        );

        std::vector<double> trip_loads(trip_load_count, 0.0);
        std::vector<double> stop_loads(stop_load_count, 0.0);
        std::vector<double> segment_loads(segment_load_count, 0.0);

        mathfp::CompensatedSum<double> total_load_sum;

        for (std::size_t i = 0; i < passenger_passengers.size(); ++i) {
            const auto passenger_count = passenger_passengers[i].get();
            total_load_sum.add(passenger_count);

            trip_loads[trip_indices[i]] += passenger_count;
            stop_loads[stop_indices[i]] += passenger_count;
            segment_loads[segment_indices[i]] += passenger_count;
        }

        const auto significant_loads =
              significant_load_count(segment_loads, policy.load_accumulation_threshold)
            + significant_load_count(stop_loads, policy.load_accumulation_threshold)
            + significant_load_count(trip_loads, policy.load_accumulation_threshold);

        return LoadAccumulationResult{
              .segment_loads = std::move(segment_loads)
            , .stop_loads = std::move(stop_loads)
            , .trip_loads = std::move(trip_loads)
            , .significant_loads = significant_loads
            , .total_load = total_load_sum.value()
        };
    }

}  // namespace timetable::domain::assignment
