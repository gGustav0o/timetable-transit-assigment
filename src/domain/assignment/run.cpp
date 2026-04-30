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
                    , disabled->skim_config
                );
            }

            const auto& calculated = std::get<AssignmentPipelineCalculatedResult>(result);
            return build_assignment_output(
                  calculated.input
                , calculated.network
                , calculated.search
                , calculated.choice
                , calculated.split
                , calculated.skim_config
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
