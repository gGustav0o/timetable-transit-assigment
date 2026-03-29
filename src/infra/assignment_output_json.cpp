#include "timetable/infra/assignment_output_json.hpp"

#include <cstdint>
#include <fstream>
#include <iterator>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <fmt/format.h>

#include "timetable/domain/segment_semantics.hpp"

namespace timetable::infra {
    namespace {

        class JsonWriter final {
        public:
            void begin_object() {
                begin_value();
                buffer_.push_back('{');
                stack_.push_back(Frame{ .kind = FrameKind::Object });
            }

            void end_object() {
                buffer_.push_back('}');
                stack_.pop_back();
            }

            void begin_array() {
                begin_value();
                buffer_.push_back('[');
                stack_.push_back(Frame{ .kind = FrameKind::Array });
            }

            void end_array() {
                buffer_.push_back(']');
                stack_.pop_back();
            }

            void key(std::string_view name) {
                auto& frame = stack_.back();
                if (!frame.first) {
                    buffer_.push_back(',');
                }
                frame.first = false;
                append_escaped_string(name);
                buffer_.push_back(':');
                frame.expecting_value = true;
            }

            void string(std::string_view value) {
                begin_value();
                append_escaped_string(value);
            }

            void integer(std::int64_t value) {
                begin_value();
                fmt::format_to(std::back_inserter(buffer_), "{}", value);
            }

            void number(double value) {
                begin_value();
                fmt::format_to(std::back_inserter(buffer_), "{:.17g}", value);
            }

            void boolean(bool value) {
                begin_value();
                buffer_.append(value ? "true" : "false");
            }

            void null() {
                begin_value();
                buffer_.append("null");
            }

            [[nodiscard]] std::string finish() && {
                return std::move(buffer_);
            }

        private:
            enum class FrameKind : std::uint8_t {
                Object
                , Array
            };

            struct Frame final {
                FrameKind kind{};
                bool first{ true };
                bool expecting_value{ false };
            };

            void begin_value() {
                if (stack_.empty()) {
                    return;
                }

                auto& frame = stack_.back();
                if (frame.kind == FrameKind::Array) {
                    if (!frame.first) {
                        buffer_.push_back(',');
                    }
                    frame.first = false;
                    return;
                }

                frame.expecting_value = false;
            }

            void append_escaped_string(std::string_view value) {
                buffer_.push_back('"');
                for (const auto ch : value) {
                    switch (ch) {
                        case '\\': buffer_.append("\\\\"); break;
                        case '"':  buffer_.append("\\\""); break;
                        case '\b': buffer_.append("\\b"); break;
                        case '\f': buffer_.append("\\f"); break;
                        case '\n': buffer_.append("\\n"); break;
                        case '\r': buffer_.append("\\r"); break;
                        case '\t': buffer_.append("\\t"); break;
                        default:
                            if (static_cast<unsigned char>(ch) < 0x20) {
                                fmt::format_to(
                                    std::back_inserter(buffer_)
                                    , "\\u{:04x}"
                                    , static_cast<unsigned int>(static_cast<unsigned char>(ch))
                                );
                            } else {
                                buffer_.push_back(ch);
                            }
                            break;
                    }
                }
                buffer_.push_back('"');
            }

            std::string buffer_{};
            std::vector<Frame> stack_{};
        };

        template <class StrongId>
        void write_strong_id(
            JsonWriter& writer
            , StrongId id
        ) {
            writer.integer(id.get());
        }

        void write_time(
            JsonWriter& writer
            , timetable::domain::Time value
        ) {
            writer.number(value.value());
        }

        void write_length(
            JsonWriter& writer
            , timetable::domain::Length value
        ) {
            writer.number(value.value());
        }

        void write_optional_time(
            JsonWriter& writer
            , const std::optional<timetable::domain::Time>& value
        ) {
            if (!value.has_value()) {
                writer.null();
                return;
            }
            write_time(writer, *value);
        }

        void write_optional_double(
            JsonWriter& writer
            , const std::optional<double>& value
        ) {
            if (!value.has_value()) {
                writer.null();
                return;
            }
            writer.number(*value);
        }

        template <class StrongId>
        void write_optional_strong_id(
            JsonWriter& writer
            , const std::optional<StrongId>& value
        ) {
            if (!value.has_value()) {
                writer.null();
                return;
            }
            write_strong_id(writer, *value);
        }

        void write_endpoint(
            JsonWriter& writer
            , const timetable::domain::WalkEndpoint& endpoint
        ) {
            writer.begin_object();
            if (std::holds_alternative<timetable::domain::StopId>(endpoint)) {
                writer.key("kind");
                writer.string("stop");
                writer.key("id");
                write_strong_id(writer, std::get<timetable::domain::StopId>(endpoint));
            } else {
                writer.key("kind");
                writer.string("zone");
                writer.key("id");
                write_strong_id(writer, std::get<timetable::domain::ZoneId>(endpoint));
            }
            writer.end_object();
        }

        void write_stop_occurrence(
            JsonWriter& writer
            , const timetable::domain::StopOccurrence& occurrence
        ) {
            writer.begin_object();
            writer.key("stop_id");
            write_strong_id(writer, occurrence.stop);
            writer.key("position");
            write_strong_id(writer, occurrence.position);
            writer.end_object();
        }

        void write_route_segment(
            JsonWriter& writer
            , const timetable::domain::RouteSegment& route_segment
        ) {
            writer.begin_object();
            writer.key("id");
            write_strong_id(writer, route_segment.id);
            writer.key("length");
            write_length(writer, route_segment.length);
            writer.key("run_time");
            write_time(writer, route_segment.run_time);
            writer.key("topology");
            writer.begin_object();
            writer.key("kind");
            writer.string(
                timetable::domain::is_line(route_segment)
                    ? "line"
                    : "walk"
            );

            if (const auto* walk = timetable::domain::walk_topology_of(route_segment)) {
                writer.key("from");
                write_endpoint(writer, walk->from);
                writer.key("to");
                write_endpoint(writer, walk->to);
                writer.key("path");
                writer.begin_array();
                for (const auto walk_link_id : walk->path) {
                    write_strong_id(writer, walk_link_id);
                }
                writer.end_array();
            } else if (const auto* line = timetable::domain::line_topology_of(route_segment)) {
                writer.key("line_id");
                write_strong_id(writer, line->line);
                writer.key("from");
                write_stop_occurrence(writer, line->from);
                writer.key("to");
                write_stop_occurrence(writer, line->to);
            }

            writer.end_object();
            writer.end_object();
        }

        void write_connection_segment(
            JsonWriter& writer
            , const timetable::domain::ConnectionSegment& connection_segment
        ) {
            writer.begin_object();
            writer.key("id");
            write_strong_id(writer, connection_segment.id);
            writer.key("route_segment_id");
            write_strong_id(writer, connection_segment.route_segment);
            writer.key("trip_id");
            write_optional_strong_id(writer, connection_segment.trip);
            writer.key("from_index");
            write_optional_strong_id(writer, connection_segment.from_index);
            writer.key("to_index");
            write_optional_strong_id(writer, connection_segment.to_index);
            writer.key("departure");
            write_optional_time(writer, connection_segment.departure);
            writer.key("arrival");
            write_optional_time(writer, connection_segment.arrival);
            writer.key("fare");
            write_optional_double(writer, connection_segment.fare);
            writer.end_object();
        }

        void write_path_segment(
            JsonWriter& writer
            , const timetable::domain::AssignmentPathSegment& path_segment
        ) {
            writer.begin_object();
            writer.key("connection_segment");
            write_connection_segment(writer, path_segment.connection_segment);
            writer.key("route_segment");
            write_route_segment(writer, path_segment.route_segment);
            writer.end_object();
        }

        void write_discovered_connection(
            JsonWriter& writer
            , const timetable::domain::assignment::DiscoveredConnection& connection
        ) {
            writer.begin_object();
            writer.key("origin");
            write_strong_id(writer, connection.origin);
            writer.key("destination");
            write_strong_id(writer, connection.destination);
            writer.key("departure");
            write_time(writer, connection.departure);
            writer.key("arrival");
            write_time(writer, connection.arrival);
            writer.key("journey_time");
            write_time(writer, connection.journey_time);
            writer.key("transfer_time");
            write_time(writer, connection.transfer_time);
            writer.key("transfers");
            write_strong_id(writer, connection.transfers);
            writer.key("fare");
            writer.number(connection.fare);
            writer.key("impedance");
            writer.number(connection.impedance);
            writer.key("segments");
            writer.begin_array();
            for (const auto segment_id : connection.segments) {
                write_strong_id(writer, segment_id);
            }
            writer.end_array();
            writer.end_object();
        }

        void write_assignment_connection(
            JsonWriter& writer
            , const timetable::domain::AssignmentConnection& connection
        ) {
            writer.begin_object();
            writer.key("summary");
            write_discovered_connection(writer, connection.summary);
            writer.key("path_segments");
            writer.begin_array();
            for (const auto& path_segment : connection.segments) {
                write_path_segment(writer, path_segment);
            }
            writer.end_array();
            writer.end_object();
        }

        void write_time_interval(
            JsonWriter& writer
            , const timetable::domain::TimeInterval& interval
        ) {
            writer.begin_object();
            writer.key("id");
            write_strong_id(writer, interval.id);
            writer.key("start");
            write_time(writer, interval.start);
            writer.key("end");
            write_time(writer, interval.end);
            writer.end_object();
        }

        void write_interval_share(
            JsonWriter& writer
            , const timetable::domain::AssignmentIntervalShare& share
        ) {
            writer.begin_object();
            writer.key("connection_index");
            write_strong_id(writer, share.connection_index);
            writer.key("passengers");
            writer.number(share.passengers);
            writer.key("probability");
            writer.number(share.probability);
            writer.key("independence");
            writer.number(share.independence);
            writer.key("split_impedance");
            writer.number(share.split_impedance);
            writer.end_object();
        }

        void write_demand_interval(
            JsonWriter& writer
            , const timetable::domain::AssignmentDemandInterval& interval
        ) {
            writer.begin_object();
            writer.key("interval");
            write_time_interval(writer, interval.interval);
            writer.key("demand_passengers");
            writer.number(interval.demand_passengers);
            writer.key("assigned_passengers");
            writer.number(interval.assigned_passengers);
            writer.key("shares");
            writer.begin_array();
            for (const auto& share : interval.shares) {
                write_interval_share(writer, share);
            }
            writer.end_array();
            writer.end_object();
        }

        void write_od_result(
            JsonWriter& writer
            , const timetable::domain::AssignmentOdResult& od_result
        ) {
            writer.begin_object();
            writer.key("origin");
            write_strong_id(writer, od_result.origin);
            writer.key("destination");
            write_strong_id(writer, od_result.destination);
            writer.key("search_connection_count");
            writer.integer(static_cast<std::int64_t>(od_result.search_connection_count));
            writer.key("chosen_connection_count");
            writer.integer(static_cast<std::int64_t>(od_result.chosen_connection_count));
            writer.key("total_demand_passengers");
            writer.number(od_result.total_demand_passengers);
            writer.key("assigned_passengers");
            writer.number(od_result.assigned_passengers);
            writer.key("connections");
            writer.begin_array();
            for (const auto& connection : od_result.connections) {
                write_assignment_connection(writer, connection);
            }
            writer.end_array();
            writer.key("intervals");
            writer.begin_array();
            for (const auto& interval : od_result.intervals) {
                write_demand_interval(writer, interval);
            }
            writer.end_array();
            writer.end_object();
        }

        void write_output_summary(
            JsonWriter& writer
            , const timetable::domain::AssignmentOutput::Summary& summary
        ) {
            writer.begin_object();
            writer.key("od_count");
            writer.integer(static_cast<std::int64_t>(summary.od_count));
            writer.key("search_connection_count");
            writer.integer(static_cast<std::int64_t>(summary.search_connection_count));
            writer.key("chosen_connection_count");
            writer.integer(static_cast<std::int64_t>(summary.chosen_connection_count));
            writer.key("demand_share_count");
            writer.integer(static_cast<std::int64_t>(summary.demand_share_count));
            writer.key("total_demand_passengers");
            writer.number(summary.total_demand_passengers);
            writer.key("assigned_passengers");
            writer.number(summary.assigned_passengers);
            writer.end_object();
        }

    }  // namespace

    std::string serialize_assignment_output_json(
        const timetable::domain::AssignmentOutput& output
    ) {
        JsonWriter writer;
        writer.begin_object();
        writer.key("schema");
        writer.string("timetable.assignment_output.v1");
        writer.key("units");
        writer.begin_object();
        writer.key("time");
        writer.string("seconds");
        writer.key("length");
        writer.string("input_length_units");
        writer.key("fare");
        writer.string("input_fare_units");
        writer.key("connection_index_scope");
        writer.string("od_local_zero_based");
        writer.end_object();
        writer.key("summary");
        write_output_summary(writer, output.summary);
        writer.key("od_results");
        writer.begin_array();
        for (const auto& od_result : output.od_results) {
            write_od_result(writer, od_result);
        }
        writer.end_array();
        writer.end_object();
        return std::move(writer).finish();
    }

    mathfp::Expected<mathfp::Unit> write_assignment_output_json(
        const timetable::domain::AssignmentOutput& output
        , const std::filesystem::path& path
    ) {
        std::error_code ec;
        if (const auto parent = path.parent_path(); !parent.empty()) {
            std::filesystem::create_directories(parent, ec);
            if (ec) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("failed to create output directory")
                        .ctx("path", parent.string())
                        .ctx("system_error", ec.message())
                );
            }
        }

        std::ofstream stream(path, std::ios::binary | std::ios::trunc);
        if (!stream.is_open()) {
            return mathfp::unexpected(
                mathfp::invalid_arg("failed to open assignment output file for writing")
                    .ctx("path", path.string())
            );
        }

        const auto json = serialize_assignment_output_json(output);
        stream.write(json.data(), static_cast<std::streamsize>(json.size()));
        if (!stream.good()) {
            return mathfp::unexpected(
                mathfp::invalid_arg("failed while writing assignment output file")
                    .ctx("path", path.string())
            );
        }

        return mathfp::kUnit;
    }

}  // namespace timetable::infra
