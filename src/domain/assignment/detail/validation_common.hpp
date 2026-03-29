#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <mathfp/core/error.hpp>
#include <mathfp/core/expected.hpp>
#include <mathfp/core/numeric_tolerance.hpp>
#include <mathfp/core/unit.hpp>

#include "grouping.hpp"
#include "timetable/domain/assignment/validation.hpp"
#include "timetable/infra/progress_bus.hpp"

namespace timetable::domain::assignment::detail::validation {

    using grouping::ConnectionTraceKey;
    using grouping::DemandKey;
    using grouping::OdKey;
    using grouping::connection_trace_key;
    using grouping::count_connections_by_od;
    using grouping::demand_key;
    using grouping::od_key;
    using grouping::trace_index_map;

    inline void warn(std::string_view message) {
        timetable::infra::progress::log(message, timetable::infra::LogLevel::Warning);
    }

    [[nodiscard]] inline bool is_finite_non_negative(double value) noexcept {
        return std::isfinite(value) && value >= 0.0;
    }

    [[nodiscard]] inline bool valid_probability(double value) noexcept {
        return std::isfinite(value) && value >= 0.0 && value <= 1.0;
    }

    [[nodiscard]] inline bool almost_equal_time(Time lhs, Time rhs) noexcept {
        return mathfp::almost_equal(lhs.value(), rhs.value());
    }

    [[nodiscard]] inline bool almost_equal_scalar(double lhs, double rhs) noexcept {
        return mathfp::almost_equal(lhs, rhs);
    }

    inline mathfp::Expected<mathfp::Unit> validate_unique_connection_traces(
        std::span<const DiscoveredConnection> connections
        , std::string_view where
    ) {
        std::map<ConnectionTraceKey, std::size_t> seen;
        for (std::size_t i = 0; i < connections.size(); ++i) {
            auto key = connection_trace_key(connections[i]);
            if (const auto [it, inserted] = seen.emplace(std::move(key), i); !inserted) {
                return mathfp::unexpected(
                    mathfp::internal_error("duplicate connection trace detected")
                        .ctx("stage", std::string(where))
                        .ctx("first_index", static_cast<std::int64_t>(it->second))
                        .ctx("duplicate_index", static_cast<std::int64_t>(i))
                        .ctx("origin", connections[i].origin.get())
                        .ctx("destination", connections[i].destination.get())
                );
            }
        }
        return mathfp::kUnit;
    }

}  // namespace timetable::domain::assignment::detail::validation
