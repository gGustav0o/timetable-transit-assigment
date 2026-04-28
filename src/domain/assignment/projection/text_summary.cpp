#include "timetable/domain/assignment/projection/text_summary.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>

#include <fmt/format.h>

#include <mathfp/core/try.hpp>

namespace timetable::domain::assignment::projection {
    namespace {

        std::string format_count(
            std::size_t value
        ) {
            return fmt::format("{}", value);
        }

        std::string format_scalar(
            double value
        ) {
            return fmt::format("{:.3f}", value);
        }

        std::string format_clock_like_time(
            Time value
        ) {
            const auto raw_seconds   = value.value();
            const auto total_seconds = static_cast<std::int64_t>(std::llround(raw_seconds));
            const auto sign          = total_seconds < 0 ? "-" : "";
            const auto abs_seconds   = total_seconds < 0 ? -total_seconds : total_seconds;
            const auto hours         = abs_seconds / 3600;
            const auto minutes       = (abs_seconds % 3600) / 60;
            const auto seconds       = abs_seconds % 60;

            return fmt::format("{}{:02d}:{:02d}:{:02d}", sign, hours, minutes, seconds);
        }

        std::string format_optional_time(
            const std::optional<Time>& value
        ) {
            return value.has_value() ? format_clock_like_time(*value) : "-";
        }

        std::string format_optional_scalar(
            const std::optional<double>& value
        ) {
            return value.has_value() ? format_scalar(*value) : "-";
        }

        std::string format_optional_transfers(
            const std::optional<TransferCount>& value
        ) {
            return value.has_value()
                ? fmt::format("{}", value->get())
                : "-";
        }

        bool should_include_od_summary(
              const AssignmentOdSummary&          od
            , const AssignmentTextSummaryOptions& options
        ) {
            if (options.include_empty_ods) {
                return true;
            }

            return !od.connections.empty()
                || od.interval_count          > 0
                || od.total_demand_passengers > 0.0
                || od.assigned_passengers     > 0.0;
        }

        void append_global_summary(
              std::string&                   out
            , const AssignmentResultSummary& summary
        ) {
            fmt::format_to(
                  std::back_inserter(out)
                , "Assignment Summary\n"
                  "OD pairs:            {}\n"
                  "Non-empty OD pairs:  {}\n"
                  "Intervals:           {}\n"
                  "Search connections:  {}\n"
                  "Chosen connections:  {}\n"
                  "Demand shares:       {}\n"
                  "Line loads:          {}\n"
                  "Trip loads:          {}\n"
                  "Segment loads:       {}\n"
                  "Total demand:        {}\n"
                  "Assigned passengers: {}\n"
                , format_count (summary.totals.od_count)
                , format_count (summary.nonempty_od_count)
                , format_count (summary.interval_count)
                , format_count (summary.totals.search_connection_count)
                , format_count (summary.totals.chosen_connection_count)
                , format_count (summary.totals.demand_share_count)
                , format_count (summary.line_load_count)
                , format_count (summary.trip_load_count)
                , format_count (summary.segment_load_count)
                , format_scalar(summary.totals.total_demand_passengers)
                , format_scalar(summary.totals.assigned_passengers)
            );
        }

        void append_od_summary(
              std::string&                        out
            , const AssignmentOdSummary&          od
            , const AssignmentTextSummaryOptions& options
        ) {
            fmt::format_to(
                  std::back_inserter(out)
                , "\nOD {} -> {}\n"
                  "  search  = {} chosen      = {} intervals    = {} shares  = {}\n"
                  "  demand  = {} assigned    = {}                               \n"
                  "  fastest = {} lowest_fare = {} min_transfers= {}\n"
                , od.origin     .get()
                , od.destination.get()
                , format_count             (od.search_connection_count)
                , format_count             (od.chosen_connection_count)
                , format_count             (od.interval_count)
                , format_count             (od.share_count)
                , format_scalar            (od.total_demand_passengers)
                , format_scalar            (od.assigned_passengers)
                , format_optional_time     (od.fastest_journey_time)
                , format_optional_scalar   (od.lowest_fare)
                , format_optional_transfers(od.minimum_transfers)
            );

            const auto limit = std::min(
                  options.max_connections_per_od
                , od.connections.size()
            );
            for (std::size_t i = 0; i < limit; ++i) {
                const auto& connection = od.connections[i];
                fmt::format_to(
                      std::back_inserter(out)
                    , "  #{} dep={} arr={} jt={} tt={} nt={} fare={} assigned={} shares={}\n"
                    , connection.index.get()
                    , format_clock_like_time(connection.departure)
                    , format_clock_like_time(connection.arrival)
                    , format_clock_like_time(connection.journey_time)
                    , format_clock_like_time(connection.transfer_time)
                    , connection.transfers.get()
                    , format_scalar(connection.fare)
                    , format_scalar(connection.assigned_passengers)
                    , format_count(connection.share_count)
                );
            }

            if (limit < od.connections.size()) {
                fmt::format_to(
                      std::back_inserter(out)
                    , "  ... {} more chosen connections\n"
                    , format_count(od.connections.size() - limit)
                );
            }
        }

    }  // namespace

    std::string format_assignment_result_summary(
          const AssignmentResultSummary&      summary
        , const AssignmentTextSummaryOptions& options
    ) {
        std::string out;
        append_global_summary(out, summary);

        std::size_t displayable_count = 0;
        for (const auto& od : summary.od_results) {
            if (should_include_od_summary(od, options)) {
                ++displayable_count;
            }
        }

        std::size_t emitted = 0;
        for (const auto& od : summary.od_results) {
            if (!should_include_od_summary(od, options)) {
                continue;
            }
            if (emitted >= options.max_od_results) {
                break;
            }
            append_od_summary(out, od, options);
            ++emitted;
        }

        const auto hidden_count = displayable_count > emitted
            ? displayable_count - emitted
            : 0;
        if (hidden_count > 0) {
            fmt::format_to(
                  std::back_inserter(out)
                , "\n... {} more OD results omitted\n"
                , format_count(hidden_count)
            );
        }

        return out;
    }

    mathfp::Expected<std::string> format_assignment_output_summary(
          const AssignmentOutput&             output
        , const AssignmentTextSummaryOptions& options
    ) {
        MATHFP_TRY_LET(
              AssignmentResultSummary
            , summary
            , build_assignment_result_summary(output)
        );
        return format_assignment_result_summary(summary, options);
    }

}  // namespace timetable::domain::assignment::projection
