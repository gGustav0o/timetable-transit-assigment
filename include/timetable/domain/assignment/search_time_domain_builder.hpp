#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include <mathfp/core/expected.hpp>

#include "timetable/domain/assignment/search_time_domain.hpp"
#include "timetable/domain/params.hpp"

namespace timetable::domain::assignment {

    /**
     * @brief Domain policy used to derive search-time padding from modeling assumptions.
     *
     * FixedFallback:
     * - engineering fallback; padding is supplied directly.
     *
     * SplitTemporalUtilityStrict:
     * - mathematically aligned with the split model;
     * - padding is derived from an admissible increment of the departure-time
     *   term in split impedance.
     *
     * Let Delta be max_departure_impedance_delta. Then the induced bounds are:
     *   before_start = Delta / (q_departure * early_departure)
     *   after_end    = Delta / (q_departure * late_departure)
     *
     * whenever the corresponding coefficient is strictly positive.
     */
    struct SearchTimePaddingPolicy final {
        enum class Kind : std::uint8_t {
              FixedFallback
            , SplitTemporalUtilityStrict
        };

        Kind kind{ Kind::FixedFallback };
        SearchTimePadding fixed{};
        Dimless           max_departure_impedance_delta{};
    };

    struct OriginSearchTimeDomain final {
        ZoneId            origin;
        SearchTimeDomain  domain{};

        auto operator<=>(const OriginSearchTimeDomain&) const = default;
    };

    struct OdSearchTimeDomain final {
        ZoneId            origin;
        ZoneId            destination;
        SearchTimeDomain  domain{};

        auto operator<=>(const OdSearchTimeDomain&) const = default;
    };

    /**
     * @brief Search-time domains induced by demand under one window mode.
     *
     * Exactly one storage slice is semantically active:
     * - Global:  global_domain is present
     * - PerOrigin: origin_domains is filled
     * - PerOd: od_domains is filled
     */
    struct SearchTimeDomainCatalog final {
        SearchWindowMode                    mode{ SearchWindowMode::Global };
        SearchTimePadding                   padding{};
        std::optional<SearchTimeDomain>     global_domain{};
        std::vector<OriginSearchTimeDomain> origin_domains{};
        std::vector<OdSearchTimeDomain>     od_domains{};
    };

    mathfp::Expected<SearchTimePaddingPolicy> make_fixed_search_time_padding_policy(
        SearchTimePadding padding
    );

    mathfp::Expected<SearchTimePaddingPolicy> make_split_temporal_utility_padding_policy(
        Dimless max_departure_impedance_delta
    );

    /**
     * @brief Resolve a padding policy into explicit asymmetric padding.
     *
     * For SplitTemporalUtility the resolution is exact with respect to the
     * split-model departure-time term. If one side has zero coefficient, a
     * finite padding on that side cannot be justified and resolution fails.
     */
    mathfp::Expected<SearchTimePadding> resolve_search_time_padding(
          const SearchTimePaddingPolicy& policy
        , const SplitParams&             split
    );

    /**
     * @brief Build demand-induced search-time domains for one execution mode.
     *
     * Only demand entries with passengers > 0 contribute to the domain.
     * Each contributing demand entry activates the interval referenced by its
     * interval id, expanded by the resolved padding policy.
     */
    mathfp::Expected<SearchTimeDomainCatalog> build_search_time_domain_catalog(
          const InputModel&               input
        , SearchWindowMode                mode
        , const SearchTimePaddingPolicy&  padding_policy
        , const SplitParams&              split
    );

    [[nodiscard]] const SearchTimeDomain* find_search_time_domain(
          const SearchTimeDomainCatalog& catalog
        , SearchDomainQuery              query
    ) noexcept;

    mathfp::Expected<mathfp::Unit> validate_search_time_domain_catalog(
        const SearchTimeDomainCatalog& catalog
    );

    // TODO: rename?
    [[nodiscard]] constexpr bool is_theoretically_strict(
        SearchTimePaddingPolicy policy
    ) noexcept {
        return policy.kind == SearchTimePaddingPolicy::Kind::SplitTemporalUtilityStrict;
    }

    // TODO: rename?
    [[nodiscard]] constexpr bool is_engineering_fallback(
        SearchTimePaddingPolicy policy
    ) noexcept {
        return policy.kind == SearchTimePaddingPolicy::Kind::FixedFallback;
    }

}  // namespace timetable::domain::assignment
