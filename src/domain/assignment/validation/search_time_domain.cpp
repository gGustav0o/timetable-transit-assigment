#include "timetable/domain/assignment/validation.hpp"

#include <map>

#include <fmt/format.h>

#include <mathfp/core/error.hpp>
#include <mathfp/core/try.hpp>
#include <mathfp/core/unit.hpp>

#include "../detail/validation_common.hpp"

namespace timetable::domain::assignment {
    namespace {

        using IntervalMap = std::map<IntervalId, const TimeInterval*>;

        struct OdIntervalKey final {
            ZoneId     origin{};
            ZoneId     destination{};
            IntervalId interval{};

            auto operator<=>(const OdIntervalKey&) const = default;
        };

        [[nodiscard]] bool has_active_positive_demand(
            const InputModel& input
        ) noexcept {
            for (const auto& demand : input.demand) {
                if (demand.passengers > 0.0) {
                    return true;
                }
            }
            return false;
        }

        mathfp::Expected<IntervalMap> build_interval_map(
            const InputModel& input
        ) {
            IntervalMap intervals;
            for (const auto& interval : input.intervals) {
                if (!intervals.emplace(interval.id, &interval).second) {
                    return mathfp::unexpected(
                        mathfp::invalid_arg("duplicate time interval id in search-time domain builder input")
                            .ctx("interval_id", interval.id.get())
                    );
                }
                if (!(interval.start.value() < interval.end.value())) {
                    return mathfp::unexpected(
                        mathfp::invalid_arg("search-time domain builder requires intervals satisfying start < end")
                            .ctx("interval_id", interval.id.get())
                            .ctx("start"      , interval.start.value())
                            .ctx("end"        , interval.end.value())
                    );
                }
            }
            return intervals;
        }

        mathfp::Expected<mathfp::Unit> validate_positive_demand_interval_references(
              const InputModel& input
            , const IntervalMap& intervals
        ) {
            for (const auto& demand : input.demand) {
                if (!(demand.passengers > 0.0)) {
                    continue;
                }
                if (!intervals.contains(demand.interval)) {
                    return mathfp::unexpected(
                        mathfp::invalid_arg("positive demand references unknown interval in search-time domain builder input")
                            .ctx("origin"     , demand.origin     .get())
                            .ctx("destination", demand.destination.get())
                            .ctx("interval_id", demand.interval   .get())
                    );
                }
            }
            return mathfp::kUnit;
        }

        mathfp::Expected<mathfp::Unit> validate_active_global_coverage(
              const SearchTimeDomainCatalog& catalog
            , const InputModel&              input
        ) {
            if (!has_active_positive_demand(input)) {
                return mathfp::kUnit;
            }
            if (!catalog.global_domain.has_value()) {
                return mathfp::unexpected(
                    mathfp::internal_error("global search-time domain catalog does not cover active demand")
                );
            }
            if (catalog.global_domain->windows.empty()) {
                return mathfp::unexpected(
                    mathfp::internal_error("global search-time domain catalog produced an empty domain despite active demand")
                );
            }
            return mathfp::kUnit;
        }

        mathfp::Expected<mathfp::Unit> validate_active_origin_coverage(
              const SearchTimeDomainCatalog& catalog
            , const InputModel&              input
        ) {
            std::map<ZoneId, bool> active_origins;
            for (const auto& demand : input.demand) {
                if (demand.passengers > 0.0) {
                    active_origins[demand.origin] = true;
                }
            }

            for (const auto& [origin, _] : active_origins) {
                const auto* domain = find_search_time_domain(
                    catalog, SearchDomainQuery{ .origin = origin, .destination = std::nullopt }
                );
                if (domain == nullptr) {
                    return mathfp::unexpected(
                        mathfp::internal_error("per-origin search-time domain catalog is missing an active origin slice")
                            .ctx("origin", origin.get())
                    );
                }
                if (domain->windows.empty()) {
                    return mathfp::unexpected(
                        mathfp::internal_error("per-origin search-time domain catalog produced an empty active origin slice")
                            .ctx("origin", origin.get())
                    );
                }
            }

            return mathfp::kUnit;
        }

        mathfp::Expected<mathfp::Unit> validate_active_od_coverage(
              const SearchTimeDomainCatalog& catalog
            , const InputModel&              input
        ) {
            std::map<OdIntervalKey, bool> active;
            for (const auto& demand : input.demand) {
                if (demand.passengers > 0.0) {
                    active[OdIntervalKey{
                          .origin      = demand.origin
                        , .destination = demand.destination
                        , .interval    = demand.interval
                    }] = true;
                }
            }

            for (const auto& [key, _] : active) {
                const auto* domain = find_search_time_domain(
                    catalog
                    , SearchDomainQuery{
                          .origin      = key.origin
                        , .destination = key.destination
                    }
                );
                if (domain == nullptr) {
                    return mathfp::unexpected(
                        mathfp::internal_error("per-OD search-time domain catalog is missing an active OD slice")
                            .ctx("origin"     , key.origin     .get())
                            .ctx("destination", key.destination.get())
                    );
                }
                if (domain->windows.empty()) {
                    return mathfp::unexpected(
                        mathfp::internal_error("per-OD search-time domain catalog produced an empty active OD slice")
                            .ctx("origin"     , key.origin     .get())
                            .ctx("destination", key.destination.get())
                    );
                }
            }

            return mathfp::kUnit;
        }

    }  // namespace

    mathfp::Expected<mathfp::Unit> validate_search_time_domain_builder_input(
          const InputModel&              input
        , SearchWindowMode               mode
        , const SearchTimePaddingPolicy& padding_policy
        , const SplitParams&             split
    ) {
        (void)mode;

        MATHFP_TRY_LET(
              IntervalMap
            , intervals
            , build_interval_map(input)
        );

        MATHFP_TRY(resolve_search_time_padding(padding_policy, split));

        if (input.demand.empty()) {
            detail::validation::warn(
                "search-time domain builder input: demand is empty; the derived domain catalog will contain only empty support"
            );
            return mathfp::kUnit;
        }

        MATHFP_TRY(validate_positive_demand_interval_references(input, intervals));

        if (!has_active_positive_demand(input)) {
            detail::validation::warn(
                "search-time domain builder input: no positive-demand entries are present; the derived domain catalog will be empty"
            );
        }

        return mathfp::kUnit;
    }

    mathfp::Expected<mathfp::Unit> validate_search_time_domain_builder_output(
          const SearchTimeDomainCatalog& catalog
        , const InputModel&              input
        , SearchWindowMode               expected_mode
    ) {
        if (catalog.mode != expected_mode) {
            return mathfp::unexpected(
                mathfp::internal_error("search-time domain catalog mode does not match the requested builder mode")
                    .ctx("expected_mode", static_cast<std::int64_t>(expected_mode))
                    .ctx("actual_mode"  , static_cast<std::int64_t>(catalog.mode))
            );
        }

        MATHFP_TRY(validate_search_time_domain_catalog(catalog));

        if (expected_mode == SearchWindowMode::Global) {
            return validate_active_global_coverage(catalog, input);
        }
        if (expected_mode == SearchWindowMode::PerOrigin) {
            return validate_active_origin_coverage(catalog, input);
        }
        return validate_active_od_coverage(catalog, input);
    }

}  // namespace timetable::domain::assignment
