#include "timetable/infra/txt_segments.hpp"

#include <cctype>
#include <fstream>
#include <string>
#include <string_view>
#include <utility>

#include <mathfp/core/error.hpp>
#include <mathfp/core/try.hpp>

#include <fmt/format.h>

#include "timetable/infra/progress_bus.hpp"
#include "timetable/infra/text_parse.hpp"

namespace timetable::infra::txt {

    namespace {

        constexpr std::size_t kExpectedLines = 11;
        constexpr std::int64_t kMissingId    = -1;

        constexpr std::string_view kCtxField = "field";
        constexpr std::string_view kCtxIndex = "index";

        constexpr std::string_view kFieldArr        = "arr";
        constexpr std::string_view kFieldDep        = "dep";
        constexpr std::string_view kFieldFare       = "fare";
        constexpr std::string_view kFieldFromStopId = "from_stop_id";
        constexpr std::string_view kFieldFromZoneId = "from_zone_id";
        constexpr std::string_view kFieldLength     = "length";
        constexpr std::string_view kFieldProfileId  = "profile_id";
        constexpr std::string_view kFieldTime       = "time";
        constexpr std::string_view kFieldToStopId   = "to_stop_id";
        constexpr std::string_view kFieldToZoneId   = "to_zone_id";
        constexpr std::string_view kFieldZoneIds    = "zone_ids";

        mathfp::Unexpected parse_error(
              const char*      message
            , std::string_view field
            , std::size_t      line_no
            , std::size_t      index
        ) {
            return mathfp::unexpected(
                mathfp::invalid_arg(message)
                .ctx(std::string(kCtxField), std::string(field))
                .ctx("line"                , static_cast<std::int64_t>(line_no))
                .ctx(std::string(kCtxIndex), static_cast<std::int64_t>(index))
            );
        }

        template <typename T, typename ParseFn>
        mathfp::Expected<std::vector<T>> parse_numeric_sequence(
              std::string_view line
            , std::string_view field
            , std::size_t      line_no
            , const char*      invalid_message
            , const char*      range_message
            , ParseFn&&        parse
        ) {
            std::vector<T> out;
            line = text_parse::trim_trailing_cr(line);

            const char* p   = line.data();
            const char* end = p + line.size();
            std::size_t idx = 0;

            while (p < end) {
                while (p < end && std::isspace(static_cast<unsigned char>(*p))) {
                    ++p;
                }
                if (p >= end) {
                    break;
                }

                const auto result = text_parse::parse_numeric_prefix<T>(
                    p, std::forward<ParseFn>(parse)
                );
                if (const auto* failure = std::get_if<text_parse::NumericParseFailure>(&result)) {
                    if (*failure == text_parse::NumericParseFailure::Invalid) {
                        return parse_error(invalid_message, field, line_no, idx);
                    }
                    if (*failure == text_parse::NumericParseFailure::Range) {
                        return parse_error(range_message, field, line_no, idx);
                    }
                    return parse_error(invalid_message, field, line_no, idx);
                }

                const auto& [value, next] = std::get<std::pair<T, const char*>>(result);
                out.push_back(value);
                p = next;
                ++idx;
            }

            return out;
        }

        mathfp::Expected<std::vector<std::int64_t>> parse_ints(
              std::string_view line
            , std::string_view field
            , std::size_t      line_no
        ) {
            return parse_numeric_sequence<std::int64_t>(
                  line
                , field
                , line_no
                , "failed to parse integer"
                , "integer out of range"
                , [](const char* begin, char** end) {
                    return std::strtoll(begin, end, 10);
                }
            );
        }

        mathfp::Expected<std::vector<double>> parse_doubles(
              std::string_view line
            , std::string_view field
            , std::size_t      line_no
        ) {
            return parse_numeric_sequence<double>(
                  line
                , field
                , line_no
                , "failed to parse floating point"
                , "floating point out of range"
                , [](const char* begin, char** end) {
                    return std::strtod(begin, end);
                }
            );
        }

    }  // namespace

    mathfp::Expected<timetable::infra::SegmentColumns> parse_segments_file(
        const std::filesystem::path& path
    ) {
        using timetable::infra::LogLevel;
        using timetable::infra::progress::log;
        using timetable::infra::progress::status;

        status("parsing: opening txt input");
        log("parsing: reading lines", LogLevel::Info);

        std::ifstream input(path);
        if (!input.is_open()) {
            return mathfp::unexpected(
                mathfp::invalid_arg("failed to open input file")
                .ctx("path", path.string())
            );
        }

        std::vector<std::string> lines;
        lines.reserve(kExpectedLines);
        for (std::string line; std::getline(input, line); ) {
            if (line.empty()) {
                continue;
            }
            lines.push_back(std::move(line));
        }

        if (lines.size() != kExpectedLines) {
            return mathfp::unexpected(
                mathfp::invalid_arg("unexpected number of lines in segment file")
                .ctx("lines"   , static_cast<std::int64_t>(lines.size()))
                .ctx("expected", static_cast<std::int64_t>(kExpectedLines))
            );
        }

        log(
              fmt::format("parsing: lines loaded = {}", lines.size())
            , LogLevel::Info
        );
        log("parsing: parsing columns", LogLevel::Info);

        SegmentColumns out;
        MATHFP_TRY_LET(
              std::vector<std::int64_t>
            , from_zone_id
            , parse_ints(lines[0], kFieldFromZoneId, 1)
        );
        MATHFP_TRY_LET(
              std::vector<std::int64_t>
            , from_stop_id
            , parse_ints(lines[1], kFieldFromStopId, 2)
        );
        MATHFP_TRY_LET(
              std::vector<std::int64_t>
            , to_zone_id
            , parse_ints(lines[2], kFieldToZoneId, 3)
        );
        MATHFP_TRY_LET(
              std::vector<std::int64_t>
            , to_stop_id
            , parse_ints(lines[3], kFieldToStopId, 4)
        );
        MATHFP_TRY_LET(
              std::vector<std::int64_t>
            , profile_id
            , parse_ints(lines[4], kFieldProfileId, 5)
        );
        MATHFP_TRY_LET(
              std::vector<double>
            , length_km
            , parse_doubles(lines[5], kFieldLength, 6)
        );
        MATHFP_TRY_LET(
              std::vector<double>
            , time_sec
            , parse_doubles(lines[6], kFieldTime, 7)
        );
        MATHFP_TRY_LET(
              std::vector<double>
            , dep_sec
            , parse_doubles(lines[7], kFieldDep, 8)
        );
        MATHFP_TRY_LET(
              std::vector<double>
            , arr_sec
            , parse_doubles(lines[8], kFieldArr, 9)
        );
        MATHFP_TRY_LET(
              std::vector<double>
            , fare
            , parse_doubles(lines[9], kFieldFare, 10)
        );
        MATHFP_TRY_LET(
              std::vector<std::int64_t>
            , zone_ids
            , parse_ints(lines[10], kFieldZoneIds, 11)
        );

        out.from_zone_id = std::move(from_zone_id);
        out.from_stop_id = std::move(from_stop_id);
        out.to_zone_id   = std::move(to_zone_id);
        out.to_stop_id   = std::move(to_stop_id);
        out.profile_id   = std::move(profile_id);
        out.trip_id     .assign(out.profile_id.size(), kMissingId);
        out.from_index  .assign(out.profile_id.size(), kMissingId);
        out.to_index    .assign(out.profile_id.size(), kMissingId);
        out.length_km    = std::move(length_km);
        out.time_sec     = std::move(time_sec);
        out.dep_sec      = std::move(dep_sec);
        out.arr_sec      = std::move(arr_sec);
        out.fare         = std::move(fare);
        out.zone_ids     = std::move(zone_ids);

        log(
            fmt::format(
                  "parsing: columns parsed; segments = {}  zones = {}"
                , out.from_zone_id.size()
                , out.zone_ids.size()
            )
            , LogLevel::Info
        );
        status("parsing: columns parsed");

        return out;
    }

}  // namespace timetable::infra::txt
