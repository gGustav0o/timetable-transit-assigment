#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include <mathfp/core/expected.hpp>
#include <mathfp/core/unit.hpp>

#include "timetable/enum_string.hpp"
#include "timetable/domain/model.hpp"

namespace timetable::domain::assignment {

    /**
     * @brief Search-time domain granularity induced by demand.
     *
     * Mathematical interpretation:
     * - PerOd is the reference formulation. For each (origin, destination), the
     *   first timed boarding is restricted to the demand-relevant departure-time
     *   domain of exactly that OD pair.
     * - PerOrigin is a conservative superset of PerOd obtained by taking the
     *   union over all destinations reachable from the origin with positive demand.
     * - Global is a conservative superset of PerOrigin obtained by taking the
     *   union over all active demand intervals in the model.
     *
     * Therefore, when the underlying windows are built correctly:
     * - PerOd is potentially exact.
     * - PerOrigin and Global do not change the feasible result set; they only
     *   admit additional search work.
     */
    enum class SearchWindowMode : std::uint8_t {
          PerOd
        , PerOrigin
        , Global
    };

    inline constexpr std::array kSearchWindowModeTokens{
          timetable::EnumStringEntry<SearchWindowMode>{
              SearchWindowMode::PerOd, "per_od"
          }
        , timetable::EnumStringEntry<SearchWindowMode>{
              SearchWindowMode::PerOrigin, "per_origin"
          }
        , timetable::EnumStringEntry<SearchWindowMode>{
              SearchWindowMode::Global, "global"
          }
    };

    [[nodiscard]] inline constexpr std::string_view to_string(
        SearchWindowMode value
    ) noexcept {
        return timetable::enum_to_string(value, kSearchWindowModeTokens);
    }

    [[nodiscard]] inline constexpr std::optional<SearchWindowMode> search_window_mode_from_string(
        std::string_view token
    ) noexcept {
        return timetable::enum_from_string(token, kSearchWindowModeTokens);
    }

    /**
     * @brief One closed departure-time window for the first timed boarding.
     *
     * Semantics:
     * - bounds are expressed in the same timetable time axis as segment
     *   departures and interval boundaries.
     * - a connection belongs to the window iff its first timed departure t
     *   satisfies begin <= t <= end.
     *
     * Invariant:
     * - begin <= end.
     */
    struct SearchTimeWindow final {
        Time begin{};
        Time end{};

        auto operator<=>(const SearchTimeWindow&) const = default;
    };

    /**
     * @brief Canonically normalized union of departure-time windows.
     *
     * Invariants:
     * - windows are sorted by begin, then end;
     * - each window satisfies begin <= end;
     * - windows are pairwise disjoint and non-touching after normalization;
     * - membership is defined by the union of all contained windows.
     *
     * This type is purely mathematical: it carries no storage policy and no
     * knowledge of demand tables or search execution strategy.
     */
    struct SearchTimeDomain final {
        std::vector<SearchTimeWindow> windows{};
    };

    /**
     * @brief Asymmetric padding applied around demand intervals.
     *
     * For an interval [L, R], the expanded search window is
     * [L - before_start, R + after_end].
     *
     * Invariant:
     * - before_start >= 0
     * - after_end >= 0
     */
    struct SearchTimePadding final {
        Time before_start{};
        Time after_end{};
    };

    /**
     * @brief Lookup key for a demand-induced search domain.
     *
     * destination:
     * - present  => OD-specific lookup
     * - absent   => origin-wide lookup
     */
    struct SearchDomainQuery final {
        ZoneId                origin;
        std::optional<ZoneId> destination{};

        auto operator<=>(const SearchDomainQuery&) const = default;
    };

    [[nodiscard]] bool contains(
          const SearchTimeWindow& window
        , Time                    departure
    ) noexcept;

    [[nodiscard]] bool contains(
          const SearchTimeDomain& domain
        , Time                    departure
    ) noexcept;

    [[nodiscard]] bool overlaps_or_touches(
          const SearchTimeWindow& lhs
        , const SearchTimeWindow& rhs
    ) noexcept;

    [[nodiscard]] bool is_normalized(
        std::span<const SearchTimeWindow> windows
    ) noexcept;

    [[nodiscard]] std::optional<SearchTimeWindow> bounds(
        const SearchTimeDomain& domain
    ) noexcept;

    [[nodiscard]] SearchTimeWindow hull(
          const SearchTimeWindow& lhs
        , const SearchTimeWindow& rhs
    ) noexcept;

    [[nodiscard]] SearchTimeDomain normalize_search_time_windows(
        std::vector<SearchTimeWindow> windows
    );

    [[nodiscard]] SearchTimeDomain normalize_search_time_domain(
        SearchTimeDomain domain
    );

    mathfp::Expected<mathfp::Unit> validate_search_time_window(
        const SearchTimeWindow& window
    );

    mathfp::Expected<SearchTimeWindow> make_search_time_window(
          Time begin
        , Time end
    );

    mathfp::Expected<mathfp::Unit> validate_search_time_padding(
        const SearchTimePadding& padding
    );

    mathfp::Expected<SearchTimePadding> make_search_time_padding(
          Time before_start
        , Time after_end
    );

    mathfp::Expected<SearchTimeDomain> make_search_time_domain(
        std::vector<SearchTimeWindow> windows
    );

    [[nodiscard]] SearchTimeWindow expand_interval_to_search_window(
          const TimeInterval&      interval
        , SearchTimePadding        padding
    ) noexcept;

    [[nodiscard]] SearchTimeDomain expand_intervals_to_search_domain(
          std::span<const TimeInterval> intervals
        , SearchTimePadding             padding
    );

    /**
     * @brief Domain-level correctness criterion for search-time pruning.
     *
     * The search-time domain is correct iff every connection that can obtain a
     * non-negligible split share under the modeled demand and temporal utility
     * has its first timed departure inside the applicable domain.
     *
     * This function validates only structural invariants of the domain object.
     * The full semantic correctness proof depends on the builder that derives
     * the domain from intervals, demand and temporal-utility assumptions.
     */
    mathfp::Expected<mathfp::Unit> validate_search_time_domain(
        const SearchTimeDomain& domain
    );

}  // namespace timetable::domain::assignment
