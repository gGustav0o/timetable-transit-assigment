#include "timetable/infra/presegmented_input.hpp"

#include <utility>

#include <boost/range/irange.hpp>

#include <fmt/format.h>

#include <mathfp/core/try.hpp>

#include "timetable/domain/state_ops.hpp"
#include "timetable/infra/progress_bus.hpp"

#include "detail/presegmented_input.hpp"

namespace timetable::infra {

    mathfp::Expected<timetable::domain::AssignmentInput> build_default_presegmented_assignment_input(
        SegmentColumns columns
    ) {
        return build_presegmented_assignment_input(
              std::move(columns)
            , PresegmentedInputBuildParams{}
        );
    }

    mathfp::Expected<timetable::domain::AssignmentInput> build_presegmented_assignment_input(
          SegmentColumns                      columns
        , const PresegmentedInputBuildParams& params
    ) {
        using detail::presegmented_input::BuildState;
        using detail::presegmented_input::RawIdSet;
        using detail::presegmented_input::SegmentRowView;
        using timetable::infra::LogLevel;
        using timetable::infra::progress::log;
        using timetable::infra::progress::status;

        status("parsing: validating columns");
        MATHFP_TRY_LET(std::size_t, n, detail::presegmented_input::validate_segment_columns(columns));

        log(
              fmt::format("parsing: validated columns; segments = {}", n)
            , LogLevel::Info
        );
        log("parsing: building zones, stops, lines", LogLevel::Info);
        MATHFP_TRY_LET(
              RawIdSet
            , zones_set
            , detail::presegmented_input::build_declared_zone_set(columns.zone_ids)
        );
        auto state = detail::presegmented_input::make_build_state(n, std::move(zones_set));

        log(
              fmt::format("parsing: zone_ids = {}", state.zones_set.size())
            , LogLevel::Info
        );
        log("parsing: building segments", LogLevel::Info);
        status("parsing: building segments (0%)");

        auto row_indices = boost::irange<std::size_t>(std::size_t{ 0 }, n);
        auto rows        = detail::presegmented_input::indexed_segment_rows(row_indices, columns);
        MATHFP_TRY_LET(
              BuildState
            , built_state
            , timetable::domain::state_ops::fold(
                  std::move(state)
                , rows
                , [&](BuildState current, SegmentRowView row) {
                    return detail::presegmented_input::process_segment_row_with_progress(
                          std::move(current)
                        , std::move(row)
                        , n
                    );
                }
            )
        );
        state = std::move(built_state);

        status("parsing: building segments (100%)");
        log(
            fmt::format(
                "parsing: segments built; route_segments = {}  connection_segments = {}"
                "  walk = {}  line = {}  timed = {}  untimed = {}  dropped_overnight = {}"
                , state.route_segments.size()
                , state.connection_segments.size()
                , state.stats.walk_segments
                , state.stats.line_segments
                , state.stats.timed_segments
                , state.stats.untimed_segments
                , state.stats.dropped_overnight
            )
            , LogLevel::Info
        );
        if (state.stats.dropped_overnight > 0) {
            log(
                fmt::format(
                      "parsing: dropped {} overnight timed segments"
                    , state.stats.dropped_overnight
                )
                , LogLevel::Warning
            );
        }

        status("parsing: validating extra zones");
        MATHFP_TRY(detail::presegmented_input::validate_extra_zones(state, params));

        status("parsing: finalizing input model");
        auto input = detail::presegmented_input::build_input_model(columns, state);
        log(
            fmt::format(
                  "parsing: model entities built; zones = {}  stops = {}  lines = {}"
                , input.zones.size()
                , input.stops.size()
                , input.lines.size()
            )
            , LogLevel::Info
        );
        status("parsing: completed");

        return detail::presegmented_input::make_assignment_input(std::move(input), std::move(state));
    }

}  // namespace timetable::infra
