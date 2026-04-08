#include "timetable/domain/assignment/search_time_domain.hpp"

#include <algorithm>
#include <utility>

#include <mathfp/core/error.hpp>
#include <mathfp/core/try.hpp>
#include <mathfp/core/unit.hpp>

namespace timetable::domain::assignment {
    namespace {

        [[nodiscard]] bool search_time_window_less(
              const SearchTimeWindow& lhs
            , const SearchTimeWindow& rhs
        ) noexcept {
            if (lhs.begin != rhs.begin) {
                return lhs.begin.value() < rhs.begin.value();
            }
            return lhs.end.value() < rhs.end.value();
        }

        [[nodiscard]] bool window_precedes_or_touches(
              const SearchTimeWindow& lhs
            , const SearchTimeWindow& rhs
        ) noexcept {
            return rhs.begin.value() <= lhs.end.value();
        }

    }  // namespace

    bool contains(
          const SearchTimeWindow& window
        , Time                    departure
    ) noexcept {
        return window.begin.value() <= departure.value()
            && departure.value() <= window.end.value();
    }

    bool contains(
          const SearchTimeDomain& domain
        , Time                    departure
    ) noexcept {
        if (domain.windows.empty()) {
            return false;
        }

        const auto it = std::lower_bound(
              domain.windows.begin()
            , domain.windows.end()
            , departure.value()
            , [](const SearchTimeWindow& window, double departure_value) {
                return window.end.value() < departure_value;
            }
        );
        return it != domain.windows.end() && contains(*it, departure);
    }

    bool overlaps_or_touches(
          const SearchTimeWindow& lhs
        , const SearchTimeWindow& rhs
    ) noexcept {
        return lhs.begin.value() <= rhs.end.value()
            && rhs.begin.value() <= lhs.end.value();
    }

    bool is_normalized(
        std::span<const SearchTimeWindow> windows
    ) noexcept {
        for (std::size_t i = 0; i < windows.size(); ++i) {
            const auto& window = windows[i];
            if (window.begin.value() > window.end.value()) {
                return false;
            }
            if (i == 0) {
                continue;
            }

            const auto& previous = windows[i - 1];
            if (search_time_window_less(window, previous)) {
                return false;
            }
            if (window.begin.value() <= previous.end.value()) {
                return false;
            }
        }
        return true;
    }

    std::optional<SearchTimeWindow> bounds(
        const SearchTimeDomain& domain
    ) noexcept {
        if (domain.windows.empty()) {
            return std::nullopt;
        }
        return SearchTimeWindow{
              .begin = domain.windows.front().begin
            , .end   = domain.windows.back().end
        };
    }

    SearchTimeWindow hull(
          const SearchTimeWindow& lhs
        , const SearchTimeWindow& rhs
    ) noexcept {
        return SearchTimeWindow{
              .begin = Time{ std::min(lhs.begin.value(), rhs.begin.value()) }
            , .end   = Time{ std::max(lhs.end.value()  , rhs.end.value()) }
        };
    }

    SearchTimeDomain normalize_search_time_windows(
        std::vector<SearchTimeWindow> windows
    ) {
        if (windows.empty()) {
            return SearchTimeDomain{};
        }

        std::sort(
              windows.begin()
            , windows.end()
            , search_time_window_less
        );

        std::vector<SearchTimeWindow> normalized;
        normalized.reserve(windows.size());
        normalized.push_back(windows.front());

        for (std::size_t i = 1; i < windows.size(); ++i) {
            const auto& candidate = windows[i];
            auto&       current   = normalized.back();
            if (window_precedes_or_touches(current, candidate)) {
                current = hull(current, candidate);
                continue;
            }
            normalized.push_back(candidate);
        }

        return SearchTimeDomain{ .windows = std::move(normalized) };
    }

    SearchTimeDomain normalize_search_time_domain(
        SearchTimeDomain domain
    ) {
        return normalize_search_time_windows(std::move(domain.windows));
    }

    mathfp::Expected<mathfp::Unit> validate_search_time_window(
        const SearchTimeWindow& window
    ) {
        if (window.begin.value() > window.end.value()) {
            return mathfp::unexpected(
                mathfp::invalid_arg("search time window begin must be <= end")
                    .ctx("begin", window.begin.value())
                    .ctx("end"  , window.end  .value())
            );
        }
        return mathfp::kUnit;
    }

    mathfp::Expected<SearchTimeWindow> make_search_time_window(
          Time begin
        , Time end
    ) {
        MATHFP_TRY(validate_search_time_window(SearchTimeWindow{
              .begin = begin
            , .end   = end
        }));

        return SearchTimeWindow{
              .begin = begin
            , .end   = end
        };
    }

    mathfp::Expected<mathfp::Unit> validate_search_time_padding(
        const SearchTimePadding& padding
    ) {
        if (padding.before_start.value() < 0.0 || padding.after_end.value() < 0.0) {
            return mathfp::unexpected(
                mathfp::invalid_arg("search time padding must be non-negative")
                    .ctx("before_start", padding.before_start.value())
                    .ctx("after_end"   , padding.after_end   .value())
            );
        }
        return mathfp::kUnit;
    }

    mathfp::Expected<SearchTimePadding> make_search_time_padding(
          Time before_start
        , Time after_end
    ) {
        MATHFP_TRY(validate_search_time_padding(SearchTimePadding{
              .before_start = before_start
            , .after_end    = after_end
        }));

        return SearchTimePadding{
              .before_start = before_start
            , .after_end    = after_end
        };
    }

    mathfp::Expected<SearchTimeDomain> make_search_time_domain(
        std::vector<SearchTimeWindow> windows
    ) {
        for (std::size_t i = 0; i < windows.size(); ++i) {
            const auto validation = validate_search_time_window(windows[i]);
            if (!validation) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("search time domain contains invalid window")
                        .ctx("window_index", static_cast<std::int64_t>(i))
                        .ctx("begin"       , windows[i].begin.value())
                        .ctx("end"         , windows[i].end  .value())
                );
            }
        }

        auto domain = normalize_search_time_windows(std::move(windows));
        return mathfp::Expected<SearchTimeDomain>(std::move(domain));
    }

    SearchTimeWindow expand_interval_to_search_window(
          const TimeInterval& interval
        , SearchTimePadding   padding
    ) noexcept {
        return SearchTimeWindow{
              .begin = Time{ interval.start.value() - padding.before_start.value() }
            , .end   = Time{ interval.end.value()   + padding.after_end   .value() }
        };
    }

    SearchTimeDomain expand_intervals_to_search_domain(
          std::span<const TimeInterval> intervals
        , SearchTimePadding             padding
    ) {
        std::vector<SearchTimeWindow> windows;
        windows.reserve(intervals.size());
        std::transform(
              intervals.begin()
            , intervals.end()
            , std::back_inserter(windows)
            , [&](const TimeInterval& interval) {
                return expand_interval_to_search_window(interval, padding);
            }
        );
        return normalize_search_time_windows(std::move(windows));
    }

    mathfp::Expected<mathfp::Unit> validate_search_time_domain(
        const SearchTimeDomain& domain
    ) {
        for (std::size_t i = 0; i < domain.windows.size(); ++i) {
            if (const auto validation = validate_search_time_window(domain.windows[i]); !validation) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("search time domain contains invalid window")
                        .ctx("window_index", static_cast<std::int64_t>(i))
                        .ctx("begin"       , domain.windows[i].begin.value())
                        .ctx("end"         , domain.windows[i].end  .value())
                );
            }
        }

        if (!is_normalized(domain.windows)) {
            return mathfp::unexpected(
                mathfp::invalid_arg("search time domain must be normalized")
            );
        }

        const auto renormalized = normalize_search_time_domain(SearchTimeDomain{ .windows = domain.windows });
        if (renormalized.windows != domain.windows) {
            return mathfp::unexpected(
                mathfp::internal_error("search time domain normalization is not idempotent on the provided domain")
            );
        }

        return mathfp::kUnit;
    }

}  // namespace timetable::domain::assignment
