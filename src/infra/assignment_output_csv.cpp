#include "timetable/infra/assignment_output_csv.hpp"

#include <cstdint>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>

#include <fmt/format.h>

#include "timetable/domain/endpoints.hpp"

namespace timetable::infra {
    namespace {

        using timetable::domain::EndpointKind;
        using timetable::domain::Length;
        using timetable::domain::RouteTopologyKind;
        using timetable::domain::Time;
        using timetable::domain::TransferCount;
        namespace projection = timetable::domain::assignment::projection;

        class CsvWriter final {
        public:
            void text(std::string_view value) {
                begin_field();
                append_escaped(value);
            }

            void number(double value) {
                begin_field();
                fmt::format_to(std::back_inserter(buffer_), "{:.17g}", value);
            }

            void integer(std::int64_t value) {
                begin_field();
                fmt::format_to(std::back_inserter(buffer_), "{}", value);
            }

            void end_row() {
                buffer_.push_back('\n');
                first_in_row_ = true;
            }

            [[nodiscard]] std::string finish() && {
                return std::move(buffer_);
            }

        private:
            void begin_field() {
                if (!first_in_row_) {
                    buffer_.push_back(',');
                }
                first_in_row_ = false;
            }

            void append_escaped(std::string_view value) {
                const auto needs_quotes =
                    value.find_first_of(",\"\r\n") != std::string_view::npos;
                if (!needs_quotes) {
                    buffer_.append(value);
                    return;
                }

                buffer_.push_back('"');
                for (const auto ch : value) {
                    if (ch == '"') {
                        buffer_.append("\"\"");
                    } else {
                        buffer_.push_back(ch);
                    }
                }
                buffer_.push_back('"');
            }

            std::string buffer_{};
            bool first_in_row_{ true };
        };

        std::string route_topology_kind_name(
            RouteTopologyKind kind
        ) {
            switch (kind) {
                case RouteTopologyKind::Line:
                    return "line";
                case RouteTopologyKind::Walk:
                    return "walk";
            }
            return "walk";
        }

        std::string endpoint_kind_name(
            EndpointKind kind
        ) {
            switch (kind) {
                case EndpointKind::Stop:
                    return "stop";
                case EndpointKind::Zone:
                    return "zone";
            }
            return "stop";
        }

        std::string load_level_name(
            projection::AssignmentLoadLevel level
        ) {
            switch (level) {
                case projection::AssignmentLoadLevel::Line:
                    return "line";
                case projection::AssignmentLoadLevel::Route:
                    return "route";
                case projection::AssignmentLoadLevel::Trip:
                    return "trip";
                case projection::AssignmentLoadLevel::Segment:
                    return "segment";
            }
            return "segment";
        }

        template <class StrongId>
        void write_strong_field(
              CsvWriter& writer
            , StrongId   value
        ) {
            writer.integer(static_cast<std::int64_t>(value.get()));
        }

        template <class StrongId>
        void write_optional_strong_field(
              CsvWriter&                     writer
            , const std::optional<StrongId>& value
        ) {
            if (!value.has_value()) {
                writer.text(std::string_view{});
                return;
            }
            write_strong_field(writer, *value);
        }

        void write_time_field(
              CsvWriter& writer
            , Time       value
        ) {
            writer.number(value.value());
        }

        void write_optional_time_field(
              CsvWriter&                 writer
            , const std::optional<Time>& value
        ) {
            if (!value.has_value()) {
                writer.text(std::string_view{});
                return;
            }
            write_time_field(writer, *value);
        }

        void write_length_field(
              CsvWriter& writer
            , Length     value
        ) {
            writer.number(value.value());
        }

        void write_optional_double_field(
              CsvWriter&                   writer
            , const std::optional<double>& value
        ) {
            if (!value.has_value()) {
                writer.text(std::string_view{});
                return;
            }
            writer.number(*value);
        }

        void write_optional_transfer_count_field(
              CsvWriter&                          writer
            , const std::optional<TransferCount>& value
        ) {
            if (!value.has_value()) {
                writer.text(std::string_view{});
                return;
            }
            write_strong_field(writer, *value);
        }

    }  // namespace

    std::string serialize_assignment_od_summary_csv(
        const projection::AssignmentCsvProjection& projection
    ) {
        CsvWriter writer;
        writer.text("origin");
        writer.text("destination");
        writer.text("search_connection_count");
        writer.text("chosen_connection_count");
        writer.text("interval_count");
        writer.text("share_count");
        writer.text("total_demand_passengers");
        writer.text("assigned_passengers");
        writer.text("fastest_journey_time");
        writer.text("lowest_fare");
        writer.text("minimum_transfers");
        writer.end_row();

        for (const auto& row : projection.od_summary_rows) {
            write_strong_field(writer, row.origin);
            write_strong_field(writer, row.destination);
            writer.integer(static_cast<std::int64_t>(row.search_connection_count));
            writer.integer(static_cast<std::int64_t>(row.chosen_connection_count));
            writer.integer(static_cast<std::int64_t>(row.interval_count));
            writer.integer(static_cast<std::int64_t>(row.share_count));
            writer.number(row.total_demand_passengers);
            writer.number(row.assigned_passengers);
            write_optional_time_field(writer, row.fastest_journey_time);
            write_optional_double_field(writer, row.lowest_fare);
            write_optional_transfer_count_field(writer, row.minimum_transfers);
            writer.end_row();
        }

        return std::move(writer).finish();
    }

    std::string serialize_assignment_connections_csv(
        const projection::AssignmentCsvProjection& projection
    ) {
        CsvWriter writer;
        writer.text("origin");
        writer.text("destination");
        writer.text("connection_index");
        writer.text("departure");
        writer.text("arrival");
        writer.text("journey_time");
        writer.text("in_vehicle_time");
        writer.text("access_time");
        writer.text("egress_time");
        writer.text("transfer_walk_time");
        writer.text("transfer_wait_time");
        writer.text("transfer_time");
        writer.text("transfers");
        writer.text("fare");
        writer.text("assigned_passengers");
        writer.text("share_count");
        writer.text("path_segment_count");
        writer.end_row();

        for (const auto& row : projection.connection_rows) {
            write_strong_field(writer, row.origin);
            write_strong_field(writer, row.destination);
            write_strong_field(writer, row.connection_index);
            write_time_field(writer, row.departure);
            write_time_field(writer, row.arrival);
            write_time_field(writer, row.journey_time);
            write_time_field(writer, row.in_vehicle_time);
            write_time_field(writer, row.access_time);
            write_time_field(writer, row.egress_time);
            write_time_field(writer, row.transfer_walk_time);
            write_time_field(writer, row.transfer_wait_time);
            write_time_field(writer, row.transfer_time);
            write_strong_field(writer, row.transfers);
            writer.number(row.fare);
            writer.number(row.assigned_passengers);
            writer.integer(static_cast<std::int64_t>(row.share_count));
            writer.integer(static_cast<std::int64_t>(row.path_segment_count));
            writer.end_row();
        }

        return std::move(writer).finish();
    }

    std::string serialize_assignment_shares_csv(
        const projection::AssignmentCsvProjection& projection
    ) {
        CsvWriter writer;
        writer.text("origin");
        writer.text("destination");
        writer.text("interval_id");
        writer.text("interval_start");
        writer.text("interval_end");
        writer.text("interval_demand_passengers");
        writer.text("interval_assigned_passengers");
        writer.text("connection_index");
        writer.text("share_passengers");
        writer.text("probability");
        writer.text("independence");
        writer.text("split_impedance");
        writer.end_row();

        for (const auto& row : projection.share_rows) {
            write_strong_field(writer, row.origin);
            write_strong_field(writer, row.destination);
            write_strong_field(writer, row.interval_id);
            write_time_field(writer, row.interval_start);
            write_time_field(writer, row.interval_end);
            writer.number(row.interval_demand_passengers);
            writer.number(row.interval_assigned_passengers);
            write_strong_field(writer, row.connection_index);
            writer.number(row.share_passengers);
            writer.number(row.probability);
            writer.number(row.independence);
            writer.number(row.split_impedance);
            writer.end_row();
        }

        return std::move(writer).finish();
    }

    std::string serialize_assignment_segments_csv(
        const projection::AssignmentCsvProjection& projection
    ) {
        CsvWriter writer;
        writer.text("origin");
        writer.text("destination");
        writer.text("connection_index");
        writer.text("path_segment_index");
        writer.text("connection_segment_id");
        writer.text("route_segment_id");
        writer.text("route_topology_kind");
        writer.text("physical_from_kind");
        writer.text("physical_from_id");
        writer.text("physical_to_kind");
        writer.text("physical_to_id");
        writer.text("route_length");
        writer.text("route_run_time");
        writer.text("walk_path_link_count");
        writer.text("line_id");
        writer.text("route_id");
        writer.text("line_from_stop_id");
        writer.text("line_from_position");
        writer.text("line_to_stop_id");
        writer.text("line_to_position");
        writer.text("trip_id");
        writer.text("connection_from_index");
        writer.text("connection_to_index");
        writer.text("departure");
        writer.text("arrival");
        writer.text("fare");
        writer.end_row();

        for (const auto& row : projection.segment_rows) {
            write_strong_field(writer, row.origin);
            write_strong_field(writer, row.destination);
            write_strong_field(writer, row.connection_index);
            writer.integer(static_cast<std::int64_t>(row.path_segment_index));
            write_strong_field(writer, row.connection_segment_id);
            write_strong_field(writer, row.route_segment_id);
            writer.text(route_topology_kind_name(row.route_topology_kind));
            writer.text(endpoint_kind_name(row.physical_from_kind));
            writer.integer(row.physical_from_id);
            writer.text(endpoint_kind_name(row.physical_to_kind));
            writer.integer(row.physical_to_id);
            write_length_field(writer, row.route_length);
            write_time_field(writer, row.route_run_time);
            writer.integer(static_cast<std::int64_t>(row.walk_path_link_count));
            write_optional_strong_field(writer, row.line_id);
            write_optional_strong_field(writer, row.route_id);
            write_optional_strong_field(writer, row.line_from_stop_id);
            write_optional_strong_field(writer, row.line_from_position);
            write_optional_strong_field(writer, row.line_to_stop_id);
            write_optional_strong_field(writer, row.line_to_position);
            write_optional_strong_field(writer, row.trip_id);
            write_optional_strong_field(writer, row.connection_from_index);
            write_optional_strong_field(writer, row.connection_to_index);
            write_optional_time_field(writer, row.departure);
            write_optional_time_field(writer, row.arrival);
            write_optional_double_field(writer, row.fare);
            writer.end_row();
        }

        return std::move(writer).finish();
    }

    std::string serialize_assignment_loads_csv(
        const projection::AssignmentCsvProjection& projection
    ) {
        CsvWriter writer;
        writer.text("load_level");
        writer.text("interval_id");
        writer.text("line_id");
        writer.text("route_id");
        writer.text("trip_id");
        writer.text("route_segment_id");
        writer.text("connection_segment_id");
        writer.text("from_stop_id");
        writer.text("from_position");
        writer.text("to_stop_id");
        writer.text("to_position");
        writer.text("departure");
        writer.text("arrival");
        writer.text("passengers");
        writer.text("passenger_segments");
        writer.text("segment_load_count");
        writer.end_row();

        for (const auto& row : projection.load_rows) {
            writer.text(load_level_name(row.level));
            write_strong_field(writer, row.interval_id);
            write_strong_field(writer, row.line_id);
            write_optional_strong_field(writer, row.route_id);
            write_optional_strong_field(writer, row.trip_id);
            write_optional_strong_field(writer, row.route_segment_id);
            write_optional_strong_field(writer, row.connection_segment_id);
            write_optional_strong_field(writer, row.from_stop_id);
            write_optional_strong_field(writer, row.from_position);
            write_optional_strong_field(writer, row.to_stop_id);
            write_optional_strong_field(writer, row.to_position);
            write_optional_time_field(writer, row.departure);
            write_optional_time_field(writer, row.arrival);
            write_optional_double_field(writer, row.passengers);
            write_optional_double_field(writer, row.passenger_segments);
            writer.integer(static_cast<std::int64_t>(row.segment_load_count));
            writer.end_row();
        }

        return std::move(writer).finish();
    }

}  // namespace timetable::infra
