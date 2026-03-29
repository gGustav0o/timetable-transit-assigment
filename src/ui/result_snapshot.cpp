#include "timetable/ui/result_snapshot.hpp"

#include <string_view>

#include <mathfp/core/try.hpp>

#include "timetable/domain/assignment/projection/text_summary.hpp"

namespace timetable::ui {
    namespace {

        std::vector<std::string> split_lines(
            std::string_view text
        ) {
            std::vector<std::string> lines{};
            std::size_t line_start = 0;

            while (line_start <= text.size()) {
                const auto line_end = text.find('\n', line_start);
                if (line_end == std::string_view::npos) {
                    lines.emplace_back(text.substr(line_start));
                    break;
                }

                lines.emplace_back(text.substr(line_start, line_end - line_start));
                line_start = line_end + 1;
            }

            if (lines.empty()) {
                lines.emplace_back();
            }

            return lines;
        }

    }  // namespace

    UiResultSnapshot make_pending_result_snapshot() {
        return UiResultSnapshot{
            .lines = {
                "Assignment result summary will appear here after a successful run."
            }
        };
    }

    UiResultSnapshot make_unavailable_result_snapshot() {
        return UiResultSnapshot{
            .lines = {
                "Assignment result is unavailable."
                , "See the status and logs panels for diagnostics."
            }
        };
    }

    mathfp::Expected<UiResultSnapshot> build_result_snapshot(
        const timetable::domain::AssignmentOutput& output
        , const UiResultSnapshotOptions& options
    ) {
        MATHFP_TRY_LET(
            std::string
            , text
            , timetable::domain::assignment::projection::format_assignment_output_summary(
                output
                , timetable::domain::assignment::projection::AssignmentTextSummaryOptions{
                    .max_od_results = options.max_od_results
                    , .max_connections_per_od = options.max_connections_per_od
                    , .include_empty_ods = options.include_empty_ods
                }
            )
        );

        return UiResultSnapshot{
            .lines = split_lines(text)
        };
    }

}  // namespace timetable::ui
