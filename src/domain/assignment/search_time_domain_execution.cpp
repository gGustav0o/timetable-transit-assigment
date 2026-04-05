#include "timetable/domain/assignment/search_time_domain_execution.hpp"

#include <map>
#include <span>

#include <mathfp/core/error.hpp>
#include <mathfp/core/try.hpp>
#include <mathfp/core/unit.hpp>

namespace timetable::domain::assignment {
    namespace {

        mathfp::Expected<mathfp::Unit> validate_unique_execution_origin_slices(
            std::span<const OriginSearchTimeDomain> domains
        ) {
            std::map<ZoneId, bool> seen;
            for (const auto& entry : domains) {
                if (!seen.emplace(entry.origin, true).second) {
                    return mathfp::unexpected(
                        mathfp::invalid_arg("search-time execution contains duplicate origin slice")
                            .ctx("origin", entry.origin.get())
                    );
                }
            }
            return mathfp::kUnit;
        }

        mathfp::Expected<SearchTimeDomainExecution> make_strict_execution(
            const SearchTimeDomainCatalog& catalog
        ) {
            if (catalog.mode == SearchWindowMode::PerOd) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("strict origin-wide search execution cannot consume per-OD search-time domains")
                        .ctx("mode", static_cast<std::int64_t>(catalog.mode))
                );
            }

            return SearchTimeDomainExecution{
                  .source_mode    = catalog.mode
                , .adaptation     = SearchTimeDomainAdaptation::Strict
                , .padding        = catalog.padding
                , .global_domain  = catalog.global_domain
                , .origin_domains = catalog.origin_domains
            };
        }

        SearchTimeDomain union_origin_domains(
            std::span<const SearchTimeDomain> domains
        ) {
            std::vector<SearchTimeWindow> windows;
            for (const auto& domain : domains) {
                windows.insert(
                      windows.end()
                    , domain.windows.begin()
                    , domain.windows.end()
                );
            }
            return normalize_search_time_windows(std::move(windows));
        }

        mathfp::Expected<SearchTimeDomainExecution> make_conservative_origin_execution(
            const SearchTimeDomainCatalog& catalog
        ) {
            if (catalog.mode != SearchWindowMode::PerOd) {
                return SearchTimeDomainExecution{
                      .source_mode    = catalog.mode
                    , .adaptation     = SearchTimeDomainAdaptation::ConservativeOriginFallback
                    , .padding        = catalog.padding
                    , .global_domain  = catalog.global_domain
                    , .origin_domains = catalog.origin_domains
                };
            }

            std::map<ZoneId, std::vector<SearchTimeDomain>> grouped;
            for (const auto& entry : catalog.od_domains) {
                grouped[entry.origin].push_back(entry.domain);
            }

            std::vector<OriginSearchTimeDomain> origin_domains;
            origin_domains.reserve(grouped.size());
            for (auto& [origin, domains] : grouped) {
                origin_domains.push_back(
                    OriginSearchTimeDomain{
                          .origin = origin
                        , .domain = union_origin_domains(domains)
                    }
                );
            }

            return SearchTimeDomainExecution{
                  .source_mode    = catalog.mode
                , .adaptation     = SearchTimeDomainAdaptation::ConservativeOriginFallback
                , .padding        = catalog.padding
                , .origin_domains = std::move(origin_domains)
            };
        }

    }  // namespace

    mathfp::Expected<SearchTimeDomainExecution> adapt_search_time_domain_for_origin_search(
          const SearchTimeDomainCatalog& catalog
        , SearchTimeDomainAdaptation     adaptation
    ) {
        MATHFP_TRY(validate_search_time_domain_catalog(catalog));

        if (adaptation == SearchTimeDomainAdaptation::Strict) {
            MATHFP_TRY_LET(SearchTimeDomainExecution, execution, make_strict_execution(catalog));
            MATHFP_TRY(validate_search_time_domain_execution(execution));
            return execution;
        }

        MATHFP_TRY_LET(SearchTimeDomainExecution, execution, make_conservative_origin_execution(catalog));
        MATHFP_TRY(validate_search_time_domain_execution(execution));
        return execution;
    }

    const SearchTimeDomain* find_origin_search_time_domain(
          const SearchTimeDomainExecution& execution
        , ZoneId                           origin
    ) noexcept {
        if (execution.global_domain.has_value()) {
            return &*execution.global_domain;
        }

        for (const auto& entry : execution.origin_domains) {
            if (entry.origin == origin) {
                return &entry.domain;
            }
        }
        return nullptr;
    }

    mathfp::Expected<mathfp::Unit> validate_search_time_domain_execution(
        const SearchTimeDomainExecution& execution
    ) {
        MATHFP_TRY(make_search_time_padding(
              execution.padding.before_start
            , execution.padding.after_end
        ));

        if (execution.global_domain.has_value()) {
            MATHFP_TRY(validate_search_time_domain(*execution.global_domain));
            if (!execution.origin_domains.empty()) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("search-time execution must not contain both global and origin domains")
                );
            }
            return mathfp::kUnit;
        }

        for (const auto& entry : execution.origin_domains) {
            MATHFP_TRY(validate_search_time_domain(entry.domain));
        }
        MATHFP_TRY(validate_unique_execution_origin_slices(execution.origin_domains));

        if (execution.adaptation == SearchTimeDomainAdaptation::Strict
            && execution.source_mode == SearchWindowMode::PerOd) {
            return mathfp::unexpected(
                mathfp::invalid_arg("strict search-time execution cannot expose per-OD source mode")
            );
        }

        return mathfp::kUnit;
    }

}  // namespace timetable::domain::assignment
