#include "timetable/infra/params_txt.hpp"

#include <fstream>
#include <iterator>
#include <string>

#include <fmt/format.h>

#include <mathfp/core/error.hpp>
#include <mathfp/core/try.hpp>
#include <mathfp/types/units.hpp>

#include "detail/params_txt.hpp"
#include "timetable/infra/progress_bus.hpp"

namespace timetable::infra::params_txt {
    namespace {

        mathfp::Expected<detail::Object> parse_params_root_file(
            const std::filesystem::path& path
        ) {
            using timetable::infra::LogLevel;
            using timetable::infra::progress::log;
            using timetable::infra::progress::status;

            status("parsing: opening params.txt");
            std::ifstream input(path);
            if (!input.is_open()) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("failed to open params file")
                    .ctx("path", path.string())
                );
            }

            const std::string text{
                  std::istreambuf_iterator<char>(input)
                , std::istreambuf_iterator<char>()
            };
            log(
                fmt::format(
                      "parsing: params.txt loaded; bytes = {}"
                    , text.size()
                )
                , LogLevel::Info
            );

            status("parsing: parsing params.txt syntax");
            MATHFP_TRY_LET(detail::Value, root, detail::parse_value_text(text, path.string()));
            log("parsing: params.txt syntax parsed", LogLevel::Info);

            status("parsing: validating params.txt root");
            MATHFP_TRY_LET(const detail::Object*, root_obj, detail::as_object(root, "root"));
            log("parsing: params.txt root object validated", LogLevel::Info);
            return *root_obj;
        }

    }  // namespace

    mathfp::Expected<timetable::domain::SearchParams> parse_search_params_file(
        const std::filesystem::path& path
    ) {
        using timetable::infra::LogLevel;
        using timetable::infra::progress::log;
        using timetable::infra::progress::status;

        MATHFP_TRY_LET(detail::Object, root_obj, parse_params_root_file(path));

        status("parsing: mapping params");
        MATHFP_TRY_LET(
              timetable::domain::SearchParams
            , params
            , detail::map_params(root_obj)
        );
        log(
            fmt::format(
                  "parsing: params mapped; max_transfers = {}  allow_start_wait = {}  allow_end_wait = {}"
                  "  min_transfer_wait_sec = {}  max_transfer_wait_sec = {}"
                  "  search_abs_tol(imp/jt) = {}/{}  choice_abs_tol(imp/jt) = {}/{}"
                , params.transfers.max_transfers.get()
                , params.transfers.allow_start_wait ? "true" : "false"
                , params.transfers.allow_end_wait   ? "true" : "false"
                , params.transfers.min_transfer_wait.value()
                , params.transfers.max_transfer_wait.value()
                , mathfp::units::as_dimless(params.search_tolerances.imp_add)
                , mathfp::units::as_dimless(params.search_tolerances.jt_add)
                , mathfp::units::as_dimless(params.choice_tolerances.imp_add)
                , mathfp::units::as_dimless(params.choice_tolerances.jt_add)
            )
            , LogLevel::Info
        );
        status("parsing: params.txt parsed");
        return params;
    }

    mathfp::Expected<timetable::domain::AssignmentRuntimeParams> parse_assignment_runtime_params_file(
        const std::filesystem::path& path
    ) {
        using timetable::infra::LogLevel;
        using timetable::infra::progress::log;
        using timetable::infra::progress::status;

        MATHFP_TRY_LET(detail::Object, root_obj, parse_params_root_file(path));

        status("parsing: mapping assignment runtime params");
        MATHFP_TRY_LET(
              timetable::domain::AssignmentRuntimeParams
            , params
            , detail::map_assignment_runtime_params(root_obj)
        );
        log(
            fmt::format(
                  "parsing: assignment runtime params mapped; max_transfers = {}  skim_enabled = {}  skim_func = {}"
                  "  min_transfer_wait_sec = {}  max_transfer_wait_sec = {}"
                  "  search_abs_tol(imp/jt) = {}/{}  choice_abs_tol(imp/jt) = {}/{}"
                  "  pre_assign_period_sec = {}  post_assign_period_sec = {}"
                  "  delete_outside_assignment_period = {}  demand_segment_basis = {}"
                  "  deactivate_direct_dominance = {}"
                  "  search_execution(formulation/projection/retention/workers/diagnostic) = {}/{}/{}/{}/{}"
                , params.search.transfers.max_transfers.get()
                , params.skim_matrix.enabled ? "true" : "false"
                , timetable::domain::assignment::to_string(params.skim_matrix.func)
                , params.search.transfers.min_transfer_wait.value()
                , params.search.transfers.max_transfer_wait.value()
                , mathfp::units::as_dimless(params.search.search_tolerances.imp_add)
                , mathfp::units::as_dimless(params.search.search_tolerances.jt_add)
                , mathfp::units::as_dimless(params.search.choice_tolerances.imp_add)
                , mathfp::units::as_dimless(params.search.choice_tolerances.jt_add)
                , params.assignment_period.pre_assign_period.value()
                , params.assignment_period.post_assign_period.value()
                , params.connection_deletion.delete_outside_assignment_period ? "true" : "false"
                , timetable::domain::assignment::to_string(params.demand_segment_time.basis)
                , params.complete_connection_dominance.deactivate_dominance_of_direct_connections
                    ? "true"
                    : "false"
                , timetable::domain::assignment::to_string(params.search_execution.formulation)
                , timetable::domain::assignment::to_string(params.search_execution.result_projection)
                , timetable::domain::assignment::to_string(params.search_execution.partial_retention_scope)
                , params.search_execution.max_parallel_batches
                , params.search_execution.diagnostic_mode ? "true" : "false"
            )
            , LogLevel::Info
        );
        status("parsing: assignment runtime params parsed");
        return params;
    }

}  // namespace timetable::infra::params_txt
