#include "timetable/domain/assignment/search_time_domain_diagnostics.hpp"

#include <fmt/format.h>

namespace timetable::domain::assignment {
    namespace {

        template <typename Range>
        [[nodiscard]] std::optional<SearchTimeWindow> overall_bounds_from_domains(
            const Range& domains
        ) noexcept {
            std::optional<SearchTimeWindow> result;
            for (const auto& entry : domains) {
                const auto b = bounds(entry.domain);
                if (!b.has_value()) {
                    continue;
                }
                if (!result.has_value()) {
                    result = b;
                    continue;
                }
                result = hull(*result, *b);
            }
            return result;
        }

    }  // namespace

    SearchTimeDomainSummary summarize(
        const SearchTimeDomain& domain
    ) noexcept {
        return SearchTimeDomainSummary{
              .window_count = domain.windows.size()
            , .empty        = domain.windows.empty()
            , .bounds       = bounds(domain)
        };
    }

    SearchTimeDomainCatalogSummary summarize(
        const SearchTimeDomainCatalog& catalog
    ) noexcept {
        SearchTimeDomainCatalogSummary out{
              .mode    = catalog.mode
            , .padding = catalog.padding
        };

        if (catalog.global_domain.has_value()) {
            const auto global = summarize(*catalog.global_domain);
            out.global_window_count = global.window_count;
            out.total_window_count += global.window_count;
            out.overall_bounds = global.bounds;
        }

        out.origin_slice_count = catalog.origin_domains.size();
        out.od_slice_count     = catalog.od_domains.size();

        for (const auto& entry : catalog.origin_domains) {
            out.total_window_count += entry.domain.windows.size();
        }
        for (const auto& entry : catalog.od_domains) {
            out.total_window_count += entry.domain.windows.size();
        }

        if (!out.overall_bounds.has_value()) {
            if (const auto b = overall_bounds_from_domains(catalog.origin_domains); b.has_value()) {
                out.overall_bounds = b;
            } else {
                out.overall_bounds = overall_bounds_from_domains(catalog.od_domains);
            }
        } else {
            if (const auto b = overall_bounds_from_domains(catalog.origin_domains); b.has_value()) {
                out.overall_bounds = hull(*out.overall_bounds, *b);
            }
            if (const auto b = overall_bounds_from_domains(catalog.od_domains); b.has_value()) {
                out.overall_bounds = hull(*out.overall_bounds, *b);
            }
        }

        return out;
    }

    SearchTimeDomainExecutionSummary summarize(
        const SearchTimeDomainExecution& execution
    ) noexcept {
        SearchTimeDomainExecutionSummary out{
              .source_mode        = execution.source_mode
            , .adaptation         = execution.adaptation
            , .has_global_domain  = execution.global_domain.has_value()
            , .origin_slice_count = execution.origin_domains.size()
        };

        if (execution.global_domain.has_value()) {
            const auto global = summarize(*execution.global_domain);
            out.total_window_count += global.window_count;
            out.overall_bounds = global.bounds;
        }
        for (const auto& entry : execution.origin_domains) {
            out.total_window_count += entry.domain.windows.size();
        }
        if (!out.overall_bounds.has_value()) {
            out.overall_bounds = overall_bounds_from_domains(execution.origin_domains);
        } else if (const auto b = overall_bounds_from_domains(execution.origin_domains); b.has_value()) {
            out.overall_bounds = hull(*out.overall_bounds, *b);
        }

        return out;
    }

    SearchTimeDomainConfigSummary summarize(
        const SearchTimeDomainConfig& config
    ) noexcept {
        return SearchTimeDomainConfigSummary{
              .requested_mode   = config.model.requested_mode
            , .architecture     = config.runtime.architecture
            , .rollout_stage    = config.runtime.rollout_stage
            , .strict_policy    = is_theoretically_strict(config.model.padding_policy)
            , .fallback_policy  = is_engineering_fallback(config.model.padding_policy)
        };
    }

    std::string format_search_time_domain_summary(
        const SearchTimeDomainSummary& summary
    ) {
        if (!summary.bounds.has_value()) {
            return fmt::format(
                  "search-time domain: windows={} empty=true"
                , summary.window_count
            );
        }
        return fmt::format(
              "search-time domain: windows={} empty=false bounds=[{:.3f}, {:.3f}]"
            , summary.window_count
            , summary.bounds->begin.value()
            , summary.bounds->end  .value()
        );
    }

    std::string format_search_time_domain_catalog_summary(
        const SearchTimeDomainCatalogSummary& summary
    ) {
        const auto bounds_text = summary.overall_bounds.has_value()
            ? fmt::format("[{:.3f}, {:.3f}]"
                , summary.overall_bounds->begin.value()
                , summary.overall_bounds->end  .value())
            : std::string{"<empty>"};

        return fmt::format(
              "search-time domain catalog: mode={} padding(before={:.3f}, after={:.3f})"
              " slices(global/origin/od)={}/{}/{} total_windows={} bounds={}"
            , static_cast<std::int64_t>(summary.mode)
            , summary.padding.before_start.value()
            , summary.padding.after_end   .value()
            , summary.global_window_count
            , summary.origin_slice_count
            , summary.od_slice_count
            , summary.total_window_count
            , bounds_text
        );
    }

    std::string format_search_time_domain_execution_summary(
        const SearchTimeDomainExecutionSummary& summary
    ) {
        const auto bounds_text = summary.overall_bounds.has_value()
            ? fmt::format("[{:.3f}, {:.3f}]"
                , summary.overall_bounds->begin.value()
                , summary.overall_bounds->end  .value())
            : std::string{"<empty>"};

        return fmt::format(
              "search-time execution: source_mode={} adaptation={} global={} origin_slices={} total_windows={} bounds={}"
            , static_cast<std::int64_t>(summary.source_mode)
            , static_cast<std::int64_t>(summary.adaptation)
            , summary.has_global_domain ? "true" : "false"
            , summary.origin_slice_count
            , summary.total_window_count
            , bounds_text
        );
    }

    std::string format_search_time_domain_config_summary(
        const SearchTimeDomainConfigSummary& summary
    ) {
        return fmt::format(
              "search-time config: requested_mode={} architecture={} rollout_stage={} strict_policy={} fallback_policy={}"
            , static_cast<std::int64_t>(summary.requested_mode)
            , static_cast<std::int64_t>(summary.architecture)
            , static_cast<std::int64_t>(summary.rollout_stage)
            , summary.strict_policy   ? "true" : "false"
            , summary.fallback_policy ? "true" : "false"
        );
    }

}  // namespace timetable::domain::assignment
