#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include <mathfp/core/expected.hpp>

#include "timetable/domain/assignment/search_time_domain_builder.hpp"

namespace timetable::domain::assignment {

    /**
     * @brief Adaptation from a demand-induced domain catalog to the current origin-wide search execution.
     *
     * Strict:
     * - preserves semantics exactly;
     * - therefore only catalogs already consumable by origin-wide search are accepted
     *   (Global and PerOrigin).
     *
     * ConservativeOriginFallback:
     * - preserves correctness by replacing a finer domain with an origin-wide superset;
     * - specifically, a PerOd catalog is coarsened to the union of all OD domains
     *   sharing the same origin.
     */
    enum class SearchTimeDomainAdaptation : std::uint8_t {
          Strict
        , ConservativeOriginFallback
    };

    /**
     * @brief Search-consumable execution-time lookup for first timed boarding.
     *
     * This is intentionally separate from SearchTimeDomainCatalog:
     * - catalog stores the mathematically modeled demand-induced domains;
     * - execution stores only what the current origin-wide search can query.
     */
    struct SearchTimeDomainExecution final {
        SearchWindowMode                      source_mode    { SearchWindowMode::Global };
        SearchTimeDomainAdaptation            adaptation     { SearchTimeDomainAdaptation::Strict };
        SearchTimePadding                     padding        {};
        std::optional<SearchTimeDomain>       global_domain  {};
        std::vector<OriginSearchTimeDomain>   origin_domains {};
    };

    mathfp::Expected<SearchTimeDomainExecution> adapt_search_time_domain_for_origin_search(
          const SearchTimeDomainCatalog& catalog
        , SearchTimeDomainAdaptation     adaptation
    );

    [[nodiscard]] const SearchTimeDomain* find_origin_search_time_domain(
          const SearchTimeDomainExecution& execution
        , ZoneId                           origin
    ) noexcept;

    mathfp::Expected<mathfp::Unit> validate_search_time_domain_execution(
        const SearchTimeDomainExecution& execution
    );

}  // namespace timetable::domain::assignment
