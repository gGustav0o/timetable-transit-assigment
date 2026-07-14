#pragma once

#include <algorithm>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <boost/container_hash/hash.hpp>

#include <mathfp/core/expected.hpp>
#include <mathfp/core/unit.hpp>
#include <mathfp/ranges/zip.hpp>

#include "timetable/domain/assignment.hpp"
#include "timetable/infra/presegmented_input.hpp"
#include "timetable/infra/segment_columns.hpp"

namespace timetable::infra::detail::presegmented_input {

    inline constexpr std::int64_t kMissingId = -1;

    inline constexpr std::size_t kStopReserveDiv      = 2;
    inline constexpr std::size_t kLineReserveDiv      = 4;
    inline constexpr std::size_t kExtraZoneReserveDiv = 16;
    inline constexpr std::size_t kReservePadding      = 1;
    inline constexpr std::size_t kProgressStep        = 100'000;

    inline constexpr std::string_view kCtxActual     = "actual";
    inline constexpr std::string_view kCtxArr        = "arr";
    inline constexpr std::string_view kCtxColumn     = "column";
    inline constexpr std::string_view kCtxCount      = "count";
    inline constexpr std::string_view kCtxDep        = "dep";
    inline constexpr std::string_view kCtxExpected   = "expected";
    inline constexpr std::string_view kCtxField      = "field";
    inline constexpr std::string_view kCtxFromIndex  = "from_index";
    inline constexpr std::string_view kCtxFromStopId = "from_stop_id";
    inline constexpr std::string_view kCtxFromZoneId = "from_zone_id";
    inline constexpr std::string_view kCtxIndex      = "index";
    inline constexpr std::string_view kCtxLength     = "length";
    inline constexpr std::string_view kCtxLineId     = "line_id";
    inline constexpr std::string_view kCtxProfileId  = "profile_id";
    inline constexpr std::string_view kCtxRouteId    = "route_id";
    inline constexpr std::string_view kCtxSample     = "sample";
    inline constexpr std::string_view kCtxStopId     = "stop_id";
    inline constexpr std::string_view kCtxTime       = "time";
    inline constexpr std::string_view kCtxToIndex    = "to_index";
    inline constexpr std::string_view kCtxToStopId   = "to_stop_id";
    inline constexpr std::string_view kCtxToZoneId   = "to_zone_id";
    inline constexpr std::string_view kCtxTripId     = "trip_id";
    inline constexpr std::string_view kCtxZoneId     = "zone_id";

    inline constexpr std::string_view kFieldFare = "fare";
    inline constexpr std::string_view kFieldFrom = "from";
    inline constexpr std::string_view kFieldTo   = "to";

    using RawIdSet = std::unordered_set<std::int64_t>;

    struct LineRouteKey final {
        timetable::domain::StopOccurrenceKey from{};
        timetable::domain::StopOccurrenceKey to{};
        std::int64_t                         line_id{};
        std::int64_t                         route_id{};

        auto operator<=>(const LineRouteKey&) const = default;
    };

    struct LineRouteKeyHash final {
        std::size_t operator()(const LineRouteKey& key) const noexcept {
            std::size_t seed = 0;
            boost::hash_combine(seed, key.from.stop.get());
            boost::hash_combine(seed, key.from.position.get());
            boost::hash_combine(seed, key.to.stop.get());
            boost::hash_combine(seed, key.to.position.get());
            boost::hash_combine(seed, key.line_id);
            boost::hash_combine(seed, key.route_id);
            return seed;
        }
    };

    struct LineRouteMetrics final {
        double length_km{};
        double time_sec{};
    };

    struct LineRouteEntry final {
        std::size_t      route_segment_index{};
        LineRouteMetrics metrics{};
    };

    struct BuildStats final {
        std::size_t walk_segments{};
        std::size_t line_segments{};
        std::size_t timed_segments{};
        std::size_t untimed_segments{};
        std::size_t dropped_overnight{};
    };

    struct BuildState final {
        RawIdSet                                                           zones_set{};
        RawIdSet                                                           stop_ids{};
        RawIdSet                                                           line_ids{};
        RawIdSet                                                           extra_zone_ids{};
        std::unordered_map<LineRouteKey, LineRouteEntry, LineRouteKeyHash> line_routes{};
        std::vector<timetable::domain::RouteSegment>                       route_segments{};
        std::vector<timetable::domain::ConnectionSegment>                  connection_segments{};
        std::int64_t                                                       next_walk_id{};
        std::int64_t                                                       next_route_segment_id{};
        std::int64_t                                                       next_connection_segment_id{};
        BuildStats                                                         stats{};
    };

    struct SegmentRowView final {
        std::size_t  index{};
        std::int64_t from_zone{};
        std::int64_t from_stop{};
        std::int64_t to_zone{};
        std::int64_t to_stop{};
        std::int64_t profile{};
        std::int64_t trip_id{};
        std::int64_t route_id{};
        std::int64_t from_index{};
        std::int64_t to_index{};
        double       length_km{};
        double       time_sec{};
        double       dep_sec{};
        double       arr_sec{};
        double       fare{};
    };

    struct SegmentSemantics final {
        timetable::domain::WalkEndpoint                  from_endpoint;
        timetable::domain::WalkEndpoint                  to_endpoint;
        bool                                             is_walk_segment{};
        std::optional<timetable::domain::StopOccurrence> from_occurrence{};
        std::optional<timetable::domain::StopOccurrence> to_occurrence{};
        std::optional<timetable::domain::RoutePosition>  connection_from_index{};
        std::optional<timetable::domain::RoutePosition>  connection_to_index{};
        std::optional<timetable::domain::Time>           dep{};
        std::optional<timetable::domain::Time>           arr{};
        std::optional<double>                            fare{};
    };

    mathfp::Expected<std::size_t> validate_segment_columns(
        const SegmentColumns& columns
    );

    mathfp::Expected<std::vector<std::int64_t>> infer_presegmented_route_ids(
        const SegmentColumns& columns
    );

    mathfp::Expected<RawIdSet> build_declared_zone_set(
        const std::vector<std::int64_t>& zone_ids
    );

    BuildState make_build_state(
          std::size_t segment_count
        , RawIdSet    zones_set
    );

    template <class IndexRange>
    auto indexed_segment_rows(
          IndexRange&           indices
        , const SegmentColumns& columns
    ) {
        return mathfp::ranges::zip_with(
            [](
                  std::size_t index
                , std::int64_t from_zone
                , std::int64_t from_stop
                , std::int64_t to_zone
                , std::int64_t to_stop
                , std::int64_t profile
                , std::int64_t trip_id
                , std::int64_t route_id
                , std::int64_t from_index
                , std::int64_t to_index
                , double length_km
                , double time_sec
                , double dep_sec
                , double arr_sec
                , double fare
            ) {
                return SegmentRowView{
                      .index      = index
                    , .from_zone  = from_zone
                    , .from_stop  = from_stop
                    , .to_zone    = to_zone
                    , .to_stop    = to_stop
                    , .profile    = profile
                    , .trip_id    = trip_id
                    , .route_id   = route_id
                    , .from_index = from_index
                    , .to_index   = to_index
                    , .length_km  = length_km
                    , .time_sec   = time_sec
                    , .dep_sec    = dep_sec
                    , .arr_sec    = arr_sec
                    , .fare       = fare
                };
            }
            , indices
            , columns.from_zone_id
            , columns.from_stop_id
            , columns.to_zone_id
            , columns.to_stop_id
            , columns.profile_id
            , columns.trip_id
            , columns.route_id
            , columns.from_index
            , columns.to_index
            , columns.length_km
            , columns.time_sec
            , columns.dep_sec
            , columns.arr_sec
            , columns.fare
        );
    }

    mathfp::Expected<mathfp::Unit> report_build_progress(
          std::size_t       index
        , std::size_t       total
        , const BuildStats& stats
    );

    mathfp::Expected<SegmentSemantics> interpret_segment_row(
        const SegmentRowView& row
    );

    mathfp::Expected<mathfp::Unit> collect_model_entities(
          BuildState&            state
        , const SegmentRowView&   row
        , const SegmentSemantics& semantics
    );

    mathfp::Expected<std::size_t> build_route_segment_for_row(
          BuildState&             state
        , const SegmentRowView&   row
        , const SegmentSemantics& semantics
    );

    mathfp::Expected<mathfp::Unit> append_connection_segment_for_row(
          BuildState&             state
        , const SegmentRowView&   row
        , const SegmentSemantics& semantics
        , std::size_t             route_segment_index
    );

    mathfp::Expected<BuildState> process_segment_row(
          BuildState     state
        , SegmentRowView row
    );

    mathfp::Expected<BuildState> process_segment_row_with_progress(
          BuildState     state
        , SegmentRowView row
        , std::size_t    count
    );

    mathfp::Expected<mathfp::Unit> validate_extra_zones(
          const BuildState&                   state
        , const PresegmentedInputBuildParams& params
    );

    timetable::domain::InputModel build_input_model(
          const SegmentColumns& columns
        , const BuildState&     state
    );

    timetable::domain::AssignmentInput make_assignment_input(
          timetable::domain::InputModel input
        , BuildState                    state
    );

}  // namespace timetable::infra::detail::presegmented_input
