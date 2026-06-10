#include "timetable/domain/assignment/run.hpp"

#include <chrono>
#include <utility>
#include <variant>

#include <fmt/format.h>

#include "detail/pipeline_internal.hpp"
#include "timetable/infra/progress_bus.hpp"

namespace timetable::domain::assignment {
    namespace {

        mathfp::Expected<AssignmentOutput> build_assignment_output_from_pipeline_result(
            const AssignmentPipelineResult& result
        ) {
            if (const auto* disabled = std::get_if<AssignmentPipelineDisabledResult>(&result)) {
                return build_assignment_disabled_output(
                      disabled->input
                    , disabled->vehicle_journey_item_capacity
                    , disabled->execution
                    , disabled->skim_config
                    , disabled->capacity_aware
                );
            }

            if (const auto* all_zone = std::get_if<AssignmentPipelineAllZoneSearchResult>(&result)) {
                return build_all_zone_search_output(
                      all_zone->input
                    , all_zone->search
                    , all_zone->vehicle_journey_item_capacity
                    , all_zone->execution
                    , all_zone->skim_config
                    , all_zone->capacity_aware
                );
            }

            if (const auto* timed = std::get_if<AssignmentPipelineTimedDiagnosticsResult>(&result)) {
                return build_timed_connection_diagnostics_output(
                      timed->input
                    , timed->search
                    , timed->vehicle_journey_item_capacity
                    , timed->execution
                    , timed->skim_config
                    , timed->capacity_aware
                );
            }

            if (const auto* od_day = std::get_if<AssignmentPipelineOdDayCalculatedResult>(&result)) {
                return build_od_day_assignment_output(
                      od_day->input
                    , od_day->network
                    , od_day->search
                    , od_day->choice
                    , od_day->split
                    , od_day->elementary_segment_loads
                    , od_day->vehicle_journey_item_capacity
                    , od_day->execution
                    , od_day->assignment_period
                    , od_day->admissibility_config
                    , od_day->skim_config
                    , od_day->capacity_aware
                );
            }

            const auto& calculated = std::get<AssignmentPipelineCalculatedResult>(result);
            return build_assignment_output(
                  calculated.input
                , calculated.network
                , calculated.search
                , calculated.choice
                , calculated.split
                , calculated.vehicle_journey_item_capacity
                , calculated.execution
                , calculated.skim_config
                , calculated.capacity_aware
            );
        }

    }  // namespace

    mathfp::Expected<AssignmentOutput> run_timetable_assignment(
        AssignmentInput input
    ) {
        const auto started_at = std::chrono::steady_clock::now();

        auto pipeline_result = detail::run_timetable_assignment_pipeline_with_context(std::move(input));
        if (!pipeline_result) {
            return mathfp::unexpected(std::move(pipeline_result.error()));
        }

        auto output = build_assignment_output_from_pipeline_result(*pipeline_result);
        if (!output) {
            return mathfp::unexpected(std::move(output.error()));
        }

        const auto finished_at = std::chrono::steady_clock::now();
        output->summary.runtime_seconds =
            std::chrono::duration<double>(finished_at - started_at).count();

        timetable::infra::progress::log(
            fmt::format(
                  "assignment runtime: {:.3f}s"
                , *output->summary.runtime_seconds
            )
        );
        timetable::infra::progress::log(
            fmt::format(
                  "assignment output loads: production=elementary_segment_loads elementary_segment_loads={} visum_segment_loads={} route_totals={} stop_totals={} overload_source=elementary_segment_loads overload_status={} overload_rows={}"
                , output->elementary_segment_loads.items.size()
                , output->loads.segment_loads.size()
                , output->loads.route_total_loads.size()
                , output->loads.stop_total_loads.size()
                , to_string(output->vehicle_journey_item_loads.status)
                , output->vehicle_journey_item_loads.items.size()
            )
        );
        timetable::infra::progress::log(
            fmt::format(
                  "paper split diagnostics: structural_day_paths={} timed_support_alternatives={} interval_admissible_Ca_alternatives={} demand_shares={} unassigned_intervals={} assigned_passengers={:.6f} unassigned_passengers={:.6f} full_path_dump={}"
                , output->summary.structural_day_path_count
                , output->summary.timed_support_alternative_count
                , output->summary.interval_admissible_split_alternative_count
                , output->summary.demand_share_count
                , output->summary.unassigned_demand_count
                , output->summary.assigned_passengers
                , output->summary.unassigned_passengers
                , output->export_profile == AssignmentOutputExportProfile::DiagnosticFullPath
                    ? "enabled_diagnostic"
                    : "disabled_by_default"
            )
        );

        return output;
    }

}  // namespace timetable::domain::assignment
