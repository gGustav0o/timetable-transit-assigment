#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include <mathfp/core/expected.hpp>

#include "timetable/enum_string.hpp"
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

    inline constexpr std::array kSearchTimeDomainAdaptationTokens{
          timetable::EnumStringEntry<SearchTimeDomainAdaptation>{
              SearchTimeDomainAdaptation::Strict, "strict"
          }
        , timetable::EnumStringEntry<SearchTimeDomainAdaptation>{
              SearchTimeDomainAdaptation::ConservativeOriginFallback,
              "conservative_origin_fallback"
          }
    };

    [[nodiscard]] inline constexpr std::string_view to_string(
        SearchTimeDomainAdaptation value
    ) noexcept {
        return timetable::enum_to_string(value, kSearchTimeDomainAdaptationTokens);
    }

    [[nodiscard]] inline constexpr std::optional<SearchTimeDomainAdaptation> search_time_domain_adaptation_from_string(
        std::string_view token
    ) noexcept {
        return timetable::enum_from_string(token, kSearchTimeDomainAdaptationTokens);
    }

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
