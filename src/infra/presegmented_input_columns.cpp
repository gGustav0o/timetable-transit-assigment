#include "detail/presegmented_input.hpp"

#include <array>
#include <compare>
#include <map>
#include <optional>
#include <string>
#include <utility>

#include <mathfp/core/error.hpp>
#include <mathfp/core/traverse.hpp>
#include <mathfp/core/try.hpp>

namespace timetable::infra::detail::presegmented_input {
    namespace {

        struct ColumnLengthSpec final {
            std::string_view name{};
            std::size_t      actual{};
        };

        mathfp::Expected<mathfp::Unit> validate_column_length(
              const ColumnLengthSpec& spec
            , std::size_t             expected
        ) {
            if (spec.actual != expected) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("segment column length mismatch")
                        .ctx(std::string(kCtxColumn)  , std::string(spec.name))
                        .ctx(std::string(kCtxExpected), static_cast<std::int64_t>(expected))
                        .ctx(std::string(kCtxActual)  , static_cast<std::int64_t>(spec.actual))
                );
            }
            return mathfp::kUnit;
        }

        template <std::size_t N>
        mathfp::Expected<mathfp::Unit> validate_column_lengths(
              const std::array<ColumnLengthSpec, N>& specs
            , std::size_t                            expected
        ) {
            MATHFP_TRY_LET(
                  std::vector<mathfp::Unit>
                , validated
                , mathfp::trv::traverse(specs, [&](const ColumnLengthSpec& spec) {
                    return validate_column_length(spec, expected);
                })
            );
            (void)validated;
            return mathfp::kUnit;
        }

    }  // namespace

    mathfp::Expected<std::size_t> validate_segment_columns(
        const SegmentColumns& columns
    ) {
        const auto n = columns.from_zone_id.size();
        if (n == 0) {
            return mathfp::unexpected(
                mathfp::invalid_arg("segment file contains no segments")
            );
        }

        const auto specs = std::array{
              ColumnLengthSpec{ .name = "from_stop_id", .actual = columns.from_stop_id.size() }
            , ColumnLengthSpec{ .name = "to_zone_id"  , .actual = columns.to_zone_id  .size() }
            , ColumnLengthSpec{ .name = "to_stop_id"  , .actual = columns.to_stop_id  .size() }
            , ColumnLengthSpec{ .name = "profile_id"  , .actual = columns.profile_id  .size() }
            , ColumnLengthSpec{ .name = "trip_id"     , .actual = columns.trip_id     .size() }
            , ColumnLengthSpec{ .name = "from_index"  , .actual = columns.from_index  .size() }
            , ColumnLengthSpec{ .name = "to_index"    , .actual = columns.to_index    .size() }
            , ColumnLengthSpec{ .name = "length"      , .actual = columns.length_km   .size() }
            , ColumnLengthSpec{ .name = "time"        , .actual = columns.time_sec    .size() }
            , ColumnLengthSpec{ .name = "dep"         , .actual = columns.dep_sec     .size() }
            , ColumnLengthSpec{ .name = "arr"         , .actual = columns.arr_sec     .size() }
            , ColumnLengthSpec{ .name = "fare"        , .actual = columns.fare        .size() }
        };
        MATHFP_TRY(validate_column_lengths(specs, n));
        if (!columns.route_id.empty()) {
            MATHFP_TRY(validate_column_length(
                  ColumnLengthSpec{ .name = "route_id", .actual = columns.route_id.size() }
                , n
            ));
        }

        return n;
    }

    mathfp::Expected<std::vector<std::int64_t>> infer_presegmented_route_ids(
        const SegmentColumns& columns
    ) {
        struct TripDraft final {
            std::int64_t line_id{ kMissingId };
            bool indexed{ true };
            std::map<std::int64_t, std::int64_t> stops_by_position{};
        };

        struct RouteSignature final {
            std::int64_t line_id{};
            std::vector<std::pair<std::int64_t, std::int64_t>> stops{};
            std::optional<std::int64_t> fallback_trip_id{};

            auto operator<=>(const RouteSignature&) const = default;
        };

        std::map<std::int64_t, TripDraft> trip_drafts;
        const auto n = columns.profile_id.size();

        for (std::size_t i = 0; i < n; ++i) {
            const auto line_id = columns.profile_id[i];
            if (line_id == kMissingId) {
                continue;
            }

            const auto trip_id = columns.trip_id[i];
            if (trip_id == kMissingId) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("timed segment must have trip_id for route inference")
                        .ctx(std::string(kCtxIndex), static_cast<std::int64_t>(i))
                        .ctx(std::string(kCtxLineId), line_id)
                );
            }

            auto& draft = trip_drafts[trip_id];
            if (draft.line_id == kMissingId) {
                draft.line_id = line_id;
            } else if (draft.line_id != line_id) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("one trip_id is attached to multiple line_id values")
                        .ctx(std::string(kCtxTripId), trip_id)
                        .ctx("first_line_id", draft.line_id)
                        .ctx("actual_line_id", line_id)
                );
            }

            const auto has_from_index = columns.from_index[i] != kMissingId;
            const auto has_to_index   = columns.to_index[i]   != kMissingId;
            if (!(has_from_index && has_to_index)) {
                draft.indexed = false;
                continue;
            }

            const auto put_stop = [&](std::int64_t position, std::int64_t stop_id)
                -> mathfp::Expected<mathfp::Unit> {
                const auto [it, inserted] = draft.stops_by_position.emplace(position, stop_id);
                if (inserted || it->second == stop_id) {
                    return mathfp::kUnit;
                }
                return mathfp::unexpected(
                    mathfp::invalid_arg("conflicting stop_id values for one trip route position")
                        .ctx(std::string(kCtxTripId), trip_id)
                        .ctx("position", position)
                        .ctx("first_stop_id", it->second)
                        .ctx("actual_stop_id", stop_id)
                );
            };

            MATHFP_TRY(put_stop(columns.from_index[i], columns.from_stop_id[i]));
            MATHFP_TRY(put_stop(columns.to_index[i], columns.to_stop_id[i]));
        }

        std::map<RouteSignature, std::int64_t> route_ids_by_signature;
        std::map<std::int64_t, std::int64_t> trip_route_ids;
        std::int64_t next_route_id = 0;

        for (const auto& [trip_id, draft] : trip_drafts) {
            RouteSignature signature{
                  .line_id = draft.line_id
                , .stops   = {}
                , .fallback_trip_id = std::nullopt
            };
            if (draft.indexed && draft.stops_by_position.size() >= 2) {
                signature.stops.assign(
                      draft.stops_by_position.begin()
                    , draft.stops_by_position.end()
                );
            } else {
                signature.fallback_trip_id = trip_id;
            }

            auto [it, inserted] = route_ids_by_signature.emplace(signature, next_route_id);
            if (inserted) {
                ++next_route_id;
            }
            trip_route_ids.emplace(trip_id, it->second);
        }

        std::vector<std::int64_t> route_ids(n, kMissingId);
        for (std::size_t i = 0; i < n; ++i) {
            if (columns.profile_id[i] == kMissingId) {
                continue;
            }
            const auto route_it = trip_route_ids.find(columns.trip_id[i]);
            if (route_it != trip_route_ids.end()) {
                route_ids[i] = route_it->second;
            }
        }

        return route_ids;
    }

    mathfp::Expected<RawIdSet> build_declared_zone_set(
        const std::vector<std::int64_t>& zone_ids
    ) {
        RawIdSet zones_set;
        zones_set.reserve(zone_ids.size());
        for (std::size_t i = 0; i < zone_ids.size(); ++i) {
            const auto id = zone_ids[i];
            if (id < 0) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("zone_ids must be non-negative")
                        .ctx(std::string(kCtxZoneId), id)
                        .ctx(std::string(kCtxIndex) , static_cast<std::int64_t>(i))
                );
            }
            if (!zones_set.insert(id).second) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("duplicate zone_id")
                        .ctx(std::string(kCtxZoneId), id)
                        .ctx(std::string(kCtxIndex) , static_cast<std::int64_t>(i))
                );
            }
        }
        return zones_set;
    }

    BuildState make_build_state(
          std::size_t segment_count
        , RawIdSet    zones_set
    ) {
        BuildState state;
        state.zones_set = std::move(zones_set);

        state.stop_ids           .reserve(segment_count / kStopReserveDiv + kReservePadding);
        state.line_ids           .reserve(segment_count / kLineReserveDiv + kReservePadding);
        state.extra_zone_ids     .reserve(segment_count / kExtraZoneReserveDiv + kReservePadding);
        state.line_routes        .reserve(segment_count / kLineReserveDiv + kReservePadding);
        state.route_segments     .reserve(segment_count);
        state.connection_segments.reserve(segment_count);

        return state;
    }

}  // namespace timetable::infra::detail::presegmented_input
