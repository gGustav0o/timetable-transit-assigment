#include "timetable/infra/params_txt.hpp"

#include <fstream>
#include <iterator>
#include <string>

#include <fmt/format.h>

#include <mathfp/core/error.hpp>
#include <mathfp/core/try.hpp>

#include "detail/params_txt.hpp"
#include "timetable/infra/progress_bus.hpp"

namespace timetable::infra::params_txt {

    mathfp::Expected<timetable::domain::SearchParams> parse_search_params_file(
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

        status("parsing: mapping params");
        MATHFP_TRY_LET(
              timetable::domain::SearchParams
            , params
            , detail::map_params(*root_obj)
        );
        log(
            fmt::format(
                  "parsing: params mapped; max_transfers = {}  allow_start_wait = {}  allow_end_wait = {}"
                , params.transfers.max_transfers.get()
                , params.transfers.allow_start_wait ? "true" : "false"
                , params.transfers.allow_end_wait   ? "true" : "false"
            )
            , LogLevel::Info
        );
        status("parsing: params.txt parsed");
        return params;
    }

}  // namespace timetable::infra::params_txt
