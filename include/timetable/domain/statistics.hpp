#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <mathfp/core/error.hpp>
#include <mathfp/core/expected.hpp>
#include <mathfp/core/try.hpp>
#include <mathfp/core/unit.hpp>

namespace timetable::domain::statistics {

    [[nodiscard]] inline mathfp::Expected<mathfp::Unit> ensure_nonempty(
        std::span<const double> values
        , std::string_view quantity
        , std::string_view statistic
    ) {
        if (values.empty()) {
            return mathfp::unexpected(
                mathfp::invalid_arg(
                    "cannot compute "
                    + std::string(statistic)
                    + " of empty "
                    + std::string(quantity)
                    + " list"
                )
            );
        }

        return mathfp::kUnit;
    }

    [[nodiscard]] inline mathfp::Expected<mathfp::Unit> ensure_finite(
        std::span<const double> values
        , std::string_view quantity
    ) {
        for (const auto value : values) {
            if (!std::isfinite(value)) {
                return mathfp::unexpected(
                    mathfp::domain_error("non-finite " + std::string(quantity))
                        .ctx(std::string(quantity), value)
                );
            }
        }

        return mathfp::kUnit;
    }

    [[nodiscard]] inline mathfp::Expected<double> mean(
        std::span<const double> values
        , std::string_view quantity = "value"
    ) {
        MATHFP_TRY(ensure_nonempty(values, quantity, "mean"));
        MATHFP_TRY(ensure_finite(values, quantity));

        double sum = 0.0;
        for (const auto value : values) {
            sum += value;
        }

        return sum / static_cast<double>(values.size());
    }

    [[nodiscard]] inline mathfp::Expected<double> minimum(
        std::span<const double> values
        , std::string_view quantity = "value"
    ) {
        MATHFP_TRY(ensure_nonempty(values, quantity, "min"));
        MATHFP_TRY(ensure_finite(values, quantity));

        double best = values.front();
        for (const auto value : values) {
            if (value < best) {
                best = value;
            }
        }

        return best;
    }

    [[nodiscard]] inline double nth_order_statistic(
        std::vector<double> values
        , std::size_t idx
    ) {
        std::nth_element(
            values.begin()
            , values.begin() + static_cast<std::ptrdiff_t>(idx)
            , values.end()
        );
        return values[idx];
    }

    [[nodiscard]] inline mathfp::Expected<double> median(
        std::vector<double> values
        , std::string_view quantity = "value"
    ) {
        MATHFP_TRY(ensure_nonempty(values, quantity, "median"));
        MATHFP_TRY(ensure_finite(values, quantity));

        const auto mid = values.size() / 2;
        if (values.size() % 2 == 1) {
            return nth_order_statistic(std::move(values), mid);
        }

        const auto upper = nth_order_statistic(values, mid);
        const auto lower = nth_order_statistic(std::move(values), mid - 1);
        return 0.5 * (lower + upper);
    }

    [[nodiscard]] inline mathfp::Expected<double> p95(
        std::vector<double> values
        , std::string_view quantity = "value"
    ) {
        MATHFP_TRY(ensure_nonempty(values, quantity, "p95"));
        MATHFP_TRY(ensure_finite(values, quantity));

        const auto idx = static_cast<std::size_t>(
            std::floor(0.95 * static_cast<double>(values.size() - 1))
        );
        return nth_order_statistic(std::move(values), idx);
    }

}  // namespace timetable::domain::statistics
