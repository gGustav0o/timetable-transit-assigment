#include "timetable/infra/assignment_output_text.hpp"

#include <fstream>
#include <string_view>

#include <mathfp/core/error.hpp>
#include <mathfp/core/try.hpp>
#include <mathfp/core/unit.hpp>

namespace timetable::infra {
    namespace {

        mathfp::Expected<mathfp::Unit> write_text_file(
              std::string_view             contents
            , const std::filesystem::path& path
        ) {
            std::ofstream stream(path, std::ios::binary | std::ios::trunc);
            if (!stream.is_open()) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("failed to open assignment output file for writing")
                        .ctx("path", path.string())
                );
            }

            stream.write(contents.data(), static_cast<std::streamsize>(contents.size()));
            if (!stream.good()) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("failed while writing assignment output file")
                        .ctx("path", path.string())
                );
            }

            return mathfp::kUnit;
        }

    }  // namespace

    mathfp::Expected<std::string> serialize_assignment_output_summary_text(
          const timetable::domain::AssignmentOutput&                                     output
        , const timetable::domain::assignment::projection::AssignmentTextSummaryOptions& options
    ) {
        return timetable::domain::assignment::projection::format_assignment_output_summary(
              output
            , options
        );
    }

    mathfp::Expected<mathfp::Unit> write_assignment_output_summary_text(
          const timetable::domain::AssignmentOutput&                                     output
        , const std::filesystem::path&                                                   path
        , const timetable::domain::assignment::projection::AssignmentTextSummaryOptions& options
    ) {
        MATHFP_TRY_LET(
              std::string
            , text
            , serialize_assignment_output_summary_text(output, options)
        );
        return write_text_file(text, path);
    }

}  // namespace timetable::infra
