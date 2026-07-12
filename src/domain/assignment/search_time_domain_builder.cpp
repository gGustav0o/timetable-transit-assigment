#include "timetable/domain/assignment/search_time_domain_builder.hpp"

#include <algorithm>
#include <map>
#include <set>
#include <span>
#include <utility>

#include <mathfp/core/error.hpp>
#include <mathfp/core/try.hpp>
#include <mathfp/core/unit.hpp>
#include <mathfp/types/units.hpp>

namespace timetable::domain::assignment {
    namespace {

        using IntervalMap = std::map<IntervalId, const TimeInterval*>;

        struct OdKey final {
            ZoneId origin;
            ZoneId destination;

            auto operator<=>(const OdKey&) const = default;
        };

        [[nodiscard]] double temporal_departure_coefficient(
              Dimless q_departure
            , Dimless side_weight
        ) noexcept {
            return mathfp::units::as_dimless(q_departure)
                 * mathfp::units::as_dimless(side_weight);
        }

        mathfp::Expected<Time> padding_from_impedance_delta(
              double      delta
            , double      coefficient
            , const char* side_name
        ) {
            if (!(coefficient > 0.0)) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("search-time padding side is not finitely resolvable from split temporal utility")
                        .ctx("side"       , side_name)
                        .ctx("coefficient", coefficient)
                        .ctx("hint"       , "q_departure and side-specific temporal utility weight must both be strictly positive")
                );
            }

            return Time{ delta / coefficient };
        }

        mathfp::Expected<IntervalMap> build_interval_map(
            const InputModel& input
        ) {
            IntervalMap intervals;
            for (const auto& interval : input.intervals) {
                if (!intervals.emplace(interval.id, &interval).second) {
                    return mathfp::unexpected(
                        mathfp::invalid_arg("duplicate interval id while building search time domain catalog")
                            .ctx("interval_id", interval.id.get())
                    );
                }
            }
            return intervals;
        }

        mathfp::Expected<const TimeInterval*> require_interval(
              const IntervalMap& intervals
            , IntervalId          id
        ) {
            const auto it = intervals.find(id);
            if (it == intervals.end()) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("demand entry references unknown interval while building search time domain catalog")
                        .ctx("interval_id", id.get())
                );
            }
            return it->second;
        }

        mathfp::Expected<SearchTimeDomain> build_domain_from_interval_ids(
              const std::set<IntervalId>& interval_ids
            , const IntervalMap&          intervals
            , SearchTimePadding           padding
        ) {
            std::vector<SearchTimeWindow> windows;
            windows.reserve(interval_ids.size());
            for (const auto interval_id : interval_ids) {
                const auto* interval = intervals.at(interval_id);
                windows.push_back(expand_interval_to_search_window(*interval, padding));
            }
            return make_search_time_domain(std::move(windows));
        }

        mathfp::Expected<mathfp::Unit> validate_unique_origin_slices(
            std::span<const OriginSearchTimeDomain> domains
        ) {
            std::map<ZoneId, bool> seen;
            for (const auto& entry : domains) {
                if (!seen.emplace(entry.origin, true).second) {
                    return mathfp::unexpected(
                        mathfp::invalid_arg("search time domain catalog contains duplicate origin slice")
                            .ctx("origin", entry.origin.get())
                    );
                }
            }
            return mathfp::kUnit;
        }

        mathfp::Expected<mathfp::Unit> validate_unique_od_slices(
            std::span<const OdSearchTimeDomain> domains
        ) {
            std::map<OdKey, bool> seen;
            for (const auto& entry : domains) {
                const auto key = OdKey{
                      .origin      = entry.origin
                    , .destination = entry.destination
                };
                if (!seen.emplace(key, true).second) {
                    return mathfp::unexpected(
                        mathfp::invalid_arg("search time domain catalog contains duplicate od slice")
                            .ctx("origin"     , entry.origin     .get())
                            .ctx("destination", entry.destination.get())
                    );
                }
            }
            return mathfp::kUnit;
        }

        mathfp::Expected<SearchTimeDomainCatalog> build_global_catalog(
              const InputModel& input
            , const IntervalMap& intervals
            , SearchTimePadding  padding
        ) {
            std::set<IntervalId> active_intervals;
            for (const auto& demand : input.demand) {
                if (!(demand.passengers > 0.0)) {
                    continue;
                }
                MATHFP_TRY(require_interval(intervals, demand.interval));
                active_intervals.insert(demand.interval);
            }

            MATHFP_TRY_LET(
                  SearchTimeDomain
                , domain
                , build_domain_from_interval_ids(active_intervals, intervals, padding)
            );

            return SearchTimeDomainCatalog{
                  .mode          = SearchWindowMode::Global
                , .padding       = padding
                , .global_domain = std::move(domain)
            };
        }

        mathfp::Expected<SearchTimeDomainCatalog> build_origin_catalog(
              const InputModel& input
            , const IntervalMap& intervals
            , SearchTimePadding  padding
        ) {
            std::map<ZoneId, std::set<IntervalId>> grouped;
            for (const auto& demand : input.demand) {
                if (!(demand.passengers > 0.0)) {
                    continue;
                }
                MATHFP_TRY(require_interval(intervals, demand.interval));
                grouped[demand.origin].insert(demand.interval);
            }

            std::vector<OriginSearchTimeDomain> domains;
            domains.reserve(grouped.size());
            for (const auto& [origin, interval_ids] : grouped) {
                MATHFP_TRY_LET(
                      SearchTimeDomain
                    , domain
                    , build_domain_from_interval_ids(interval_ids, intervals, padding)
                );
                domains.push_back(
                    OriginSearchTimeDomain{
                          .origin = origin
                        , .domain = std::move(domain)
                    }
                );
            }

            return SearchTimeDomainCatalog{
                  .mode           = SearchWindowMode::PerOrigin
                , .padding        = padding
                , .origin_domains = std::move(domains)
            };
        }

        mathfp::Expected<SearchTimeDomainCatalog> build_od_catalog(
              const InputModel& input
            , const IntervalMap& intervals
            , SearchTimePadding  padding
        ) {
            std::map<OdKey, std::set<IntervalId>> grouped;
            for (const auto& demand : input.demand) {
                if (!(demand.passengers > 0.0)) {
                    continue;
                }
                MATHFP_TRY(require_interval(intervals, demand.interval));
                grouped[OdKey{ demand.origin, demand.destination }].insert(demand.interval);
            }

            std::vector<OdSearchTimeDomain> domains;
            domains.reserve(grouped.size());
            for (const auto& [key, interval_ids] : grouped) {
                MATHFP_TRY_LET(
                      SearchTimeDomain
                    , domain
                    , build_domain_from_interval_ids(interval_ids, intervals, padding)
                );
                domains.push_back(
                    OdSearchTimeDomain{
                          .origin      = key.origin
                        , .destination = key.destination
                        , .domain      = std::move(domain)
                    }
                );
            }

            return SearchTimeDomainCatalog{
                  .mode       = SearchWindowMode::PerOd
                , .padding    = padding
                , .od_domains = std::move(domains)
            };
        }

    }  // namespace

    mathfp::Expected<SearchTimePaddingPolicy> make_fixed_search_time_padding_policy(
        SearchTimePadding padding
    ) {
        MATHFP_TRY(make_search_time_padding(
              padding.before_start
            , padding.after_end
        ));

        return SearchTimePaddingPolicy{
              .kind  = SearchTimePaddingPolicy::Kind::FixedFallback
            , .fixed = padding
        };
    }

    mathfp::Expected<SearchTimePaddingPolicy> make_split_temporal_utility_padding_policy(
        Dimless max_departure_impedance_delta
    ) {
        if (mathfp::units::as_dimless(max_departure_impedance_delta) < 0.0) {
            return mathfp::unexpected(
                mathfp::invalid_arg("max_departure_impedance_delta must be non-negative")
                    .ctx("max_departure_impedance_delta", mathfp::units::as_dimless(max_departure_impedance_delta))
            );
        }

        return SearchTimePaddingPolicy{
              .kind = SearchTimePaddingPolicy::Kind::SplitTemporalUtilityStrict
            , .max_departure_impedance_delta = max_departure_impedance_delta
        };
    }

    mathfp::Expected<SearchTimePadding> resolve_search_time_padding(
          const SearchTimePaddingPolicy& policy
        , const SplitParams&             split
    ) {
        if (policy.kind == SearchTimePaddingPolicy::Kind::FixedFallback) {
            return make_search_time_padding(
                  policy.fixed.before_start
                , policy.fixed.after_end
            );
        }

        const auto delta = mathfp::units::as_dimless(policy.max_departure_impedance_delta);
        const auto before_coefficient = temporal_departure_coefficient(
              split.q_departure
            , split.temporal_utility.early_departure
        );
        const auto after_coefficient = temporal_departure_coefficient(
              split.q_departure
            , split.temporal_utility.late_departure
        );

        MATHFP_TRY_LET(Time, before_start, padding_from_impedance_delta(
              delta
            , before_coefficient
            , "before_start"
        ));
        MATHFP_TRY_LET(Time, after_end, padding_from_impedance_delta(
              delta
            , after_coefficient
            , "after_end"
        ));

        return SearchTimePadding{
              .before_start = before_start
            , .after_end    = after_end
        };
    }

    mathfp::Expected<SearchTimeDomainCatalog> build_search_time_domain_catalog(
          const InputModel&              input
        , SearchWindowMode               mode
        , const SearchTimePaddingPolicy& padding_policy
        , const SplitParams&             split
    ) {
        MATHFP_TRY_LET(
              SearchTimePadding
            , padding
            , resolve_search_time_padding(padding_policy, split)
        );
        MATHFP_TRY_LET(
              IntervalMap
            , intervals
            , build_interval_map(input)
        );

        SearchTimeDomainCatalog catalog;
        if (mode == SearchWindowMode::Global) {
            MATHFP_TRY_LET(SearchTimeDomainCatalog, built, build_global_catalog(input, intervals, padding));
            catalog = std::move(built);
        } else if (mode == SearchWindowMode::PerOrigin) {
            MATHFP_TRY_LET(SearchTimeDomainCatalog, built, build_origin_catalog(input, intervals, padding));
            catalog = std::move(built);
        } else {
            MATHFP_TRY_LET(SearchTimeDomainCatalog, built, build_od_catalog(input, intervals, padding));
            catalog = std::move(built);
        }

        MATHFP_TRY(validate_search_time_domain_catalog(catalog));
        return catalog;
    }

    const SearchTimeDomain* find_search_time_domain(
          const SearchTimeDomainCatalog& catalog
        , SearchDomainQuery              query
    ) noexcept {
        if (catalog.mode == SearchWindowMode::Global) {
            return catalog.global_domain ? &*catalog.global_domain : nullptr;
        }

        if (catalog.mode == SearchWindowMode::PerOrigin) {
            for (const auto& entry : catalog.origin_domains) {
                if (entry.origin == query.origin) {
                    return &entry.domain;
                }
            }
            return nullptr;
        }

        if (!query.destination.has_value()) {
            return nullptr;
        }
        for (const auto& entry : catalog.od_domains) {
            if (entry.origin == query.origin && entry.destination == *query.destination) {
                return &entry.domain;
            }
        }
        return nullptr;
    }

    mathfp::Expected<mathfp::Unit> validate_search_time_domain_catalog(
        const SearchTimeDomainCatalog& catalog
    ) {
        MATHFP_TRY(make_search_time_padding(
              catalog.padding.before_start
            , catalog.padding.after_end
        ));

        std::size_t active_slices = 0;
        if (catalog.global_domain.has_value()) {
            ++active_slices;
            MATHFP_TRY(validate_search_time_domain(*catalog.global_domain));
        }
        if (!catalog.origin_domains.empty()) {
            ++active_slices;
            for (const auto& entry : catalog.origin_domains) {
                MATHFP_TRY(validate_search_time_domain(entry.domain));
            }
            MATHFP_TRY(validate_unique_origin_slices(catalog.origin_domains));
        }
        if (!catalog.od_domains.empty()) {
            ++active_slices;
            for (const auto& entry : catalog.od_domains) {
                MATHFP_TRY(validate_search_time_domain(entry.domain));
            }
            MATHFP_TRY(validate_unique_od_slices(catalog.od_domains));
        }

        if (active_slices > 1) {
            return mathfp::unexpected(
                mathfp::invalid_arg("search time domain catalog stores multiple active mode slices")
            );
        }

        if (catalog.mode == SearchWindowMode::Global) {
            if (!catalog.global_domain.has_value()) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("global search time domain catalog requires global_domain")
                );
            }
            if (!catalog.origin_domains.empty() || !catalog.od_domains.empty()) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("global search time domain catalog must not contain origin or od slices")
                );
            }
            return mathfp::kUnit;
        }

        if (catalog.mode == SearchWindowMode::PerOrigin) {
            if (catalog.global_domain.has_value() || !catalog.od_domains.empty()) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("per-origin search time domain catalog contains incompatible slices")
                );
            }
            return mathfp::kUnit;
        }

        if (catalog.global_domain.has_value() || !catalog.origin_domains.empty()) {
            return mathfp::unexpected(
                mathfp::invalid_arg("per-od search time domain catalog contains incompatible slices")
            );
        }
        return mathfp::kUnit;
    }

}  // namespace timetable::domain::assignment
