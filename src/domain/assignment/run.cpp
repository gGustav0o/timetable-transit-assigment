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

        return output;
    }

}  // namespace timetable::domain::assignment
